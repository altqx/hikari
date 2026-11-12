pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Dialogs
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280
    height: 800
    visible: true
    title: {
        if (!shell.hasEditingTarget)
            return projectName.length > 0 ? qsTr("%1 - HikariSub").arg(projectName) : "HikariSub"
        return projectName.length > 0 ? qsTr("%1 - %2 - HikariSub").arg(shell.editingTitle, projectName)
                                      : qsTr("%1 - HikariSub").arg(shell.editingTitle)
    }
    color: Theme.background

    property int shellWindow: 0
    property int projectRevision: 0
    readonly property string projectName: {
        root.projectRevision
        return root.app.projectName(root.shellWindow)
    }
    Connections {
        target: root.app
        function onProjectsChanged() { root.projectRevision++ }
        function onProjectOffered(shellWindow, folder, name) {
            if (shellWindow === root.shellWindow)
                projectOfferBar.offer(folder, name)
        }
        function onTabJoinedProject(shellWindow) {
            if (shellWindow === root.shellWindow)
                root.notices.post("info", root.app.projectMoveText(shellWindow), "")
        }
    }
    readonly property string engineSuffix: shellWindow === 0 ? "" : "~" + shellWindow
    readonly property var dockAffinities: ["w" + shellWindow]
    readonly property bool windowHome: {
        root.app.home
        return root.app.windowHome(root.shellWindow)
    }
    function engineName(name) { return name + root.engineSuffix }
    function panelName(dock) { return dock.uniqueName.slice(0, dock.uniqueName.length - root.engineSuffix.length) }
    required property ShellController shell
    required property LineEditorController editor
    required property VideoController video
    required property AudioController audio
    required property var app
    required property var automation
    required property AutomationManagerController automationManager
    required property TemplateEditorController templateEditor
    required property NodeEffectsController nodeEffects
    required property AutomationDialogController automationDialogs
    required property AutomationFilePickerController automationPicker
    required property ScriptResolutionPromptController automationResolution
    required property LogController log
    required property TagButtonsController tagButtons
    required property ColourPickerController colourPicker
    required property WorkspaceLayoutController workspaceLayout
    required property ShiftTimesController shiftTimes
    required property GridFilterController gridFilter
    required property var automationHotkeys
    required property var updates
    required property var whatsNew
    required property var analytics
    required property var starPrompt
    required property var crashReports
    required property var aiSettings
    required property var styleManager
    required property var kanjiTimer
    required property var alignToVideo
    required property var videoAnalysis
    required property var lyricsImport
    required property var qcNotes
    required property var fontCollector
    required property var fontCatalogs
    required property var matroska
    required property var review
    required property var agent
    required property var inlineAi
    required property var hotkeys
    required property var settingsImport
    required property VisualToolsController visualTools
    required property VideoViewController videoView
    required property StatusBarController statusBar
    required property VideoFullscreenController videoFullscreen

    readonly property var macroItems: {
        const out = []
        for (const script of root.automationManager.scripts)
            for (const macro of script.macros)
                out.push({ path: script.path, ordinal: macro.ordinal, name: macro.name })
        return out
    }

    readonly property var sortKeys: [
        { key: "start", label: qsTr("Start time"), all: "GLOBAL_SORT_ALL_BY_START_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_START_TIMES", help: qsTr("Sort by start time") },
        { key: "end", label: qsTr("End time"), all: "GLOBAL_SORT_ALL_BY_END_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_END_TIMES", help: qsTr("Sort by end time") },
        { key: "style", label: qsTr("Style"), all: "GLOBAL_SORT_ALL_BY_STYLE", selected: "GLOBAL_SORT_SELECTED_BY_STYLE",
          help: qsTr("Sort by styles") },
        { key: "actor", label: qsTr("Actor"), all: "GLOBAL_SORT_ALL_BY_ACTOR", selected: "GLOBAL_SORT_SELECTED_BY_ACTOR",
          help: qsTr("Sort by actor") },
        { key: "effect", label: qsTr("Effect"), all: "GLOBAL_SORT_ALL_BY_EFFECT", selected: "GLOBAL_SORT_SELECTED_BY_EFFECT",
          help: qsTr("Sort by effect") },
        { key: "layer", label: qsTr("Layer"), all: "GLOBAL_SORT_ALL_BY_LAYER", selected: "GLOBAL_SORT_SELECTED_BY_LAYER",
          help: qsTr("Sort by layer") },
        { key: "text", label: qsTr("Text"), all: "GLOBAL_SORT_ALL_BY_TEXT", selected: "GLOBAL_SORT_SELECTED_BY_TEXT",
          help: qsTr("Sort by text") },
        { key: "textWithoutTags", label: qsTr("Text without tags"), all: "GLOBAL_SORT_ALL_BY_TEXT_WITHOUT_TAGS",
          selected: "GLOBAL_SORT_SELECTED_BY_TEXT_WITHOUT_TAGS", help: qsTr("Sort by text without tags") }
    ]

    function applyTagButton(index, edited) {
        const b = root.tagButtons.pressed(index)
        if (b.tag === undefined || b.tag.length === 0)
            return false
        let field = edited ?? root.editedField()
        // MUST KILL: tag button type is a magic int (2 = plain text into the main field)
        if (b.type === 2)
            field = root.editor.translationMode && translationText.text.length > 0 ? translationText : lineText
        return root.editor.applyTagButton(field.role, b.tag, b.type, field.selectionStart, field.selectionEnd)
    }

    function beginClose(then) {
        const rows = root.app.reviewClose(then)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    function closeProject() {
        const rows = root.app.reviewCloseProject(root.shellWindow)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    function openSubtitles(path) {
        const result = root.app.reviewOpen(path)
        if (!result.ok || result.done)
            return
        else if (result.rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(result.rows)
    }
    function openChosenSubtitles(path) {
        if (root.matroska.opensAsMatroska(path))
            matroskaSubtitles.beginFile(path)
        else if (/\.ogm$/i.test(path))
            tabCommands.openVideoFile(path)
        else
            root.openSubtitles(path)
    }
    property var changedOnDisk: ({})
    property var savingAway: ({})
    property var mkvTab: undefined
    Connections {
        target: root.matroska
        function onStateChanged() {
            if (root.matroska.state === 0)
                root.mkvTab = undefined
            else if (root.mkvTab === undefined)
                root.mkvTab = root.app.home ? "home" : root.currentTab().id
        }
    }
    function checkExternalChange() {
        const id = root.currentTab().id
        if (root.app.externalChange() === "modified" && id !== undefined) {
            const shown = Object.assign({}, root.changedOnDisk)
            shown[id] = true
            root.changedOnDisk = shown
        }
    }
    function endDiskChange(id) {
        const shown = Object.assign({}, root.changedOnDisk)
        delete shown[id]
        root.changedOnDisk = shown
    }
    onActiveChanged: {
        if (active)
            checkExternalChange()
    }
    // KDDW closes every panel before onClosing, so the close is reviewed here first (Docking.guardClose).
    property bool admittingClose: false
    function admitClose() {
        if (!root.app.closeApproved(root.shellWindow)) {
            const quits = !root.app.otherShellWindowOpen(root.shellWindow)
            const rows = quits ? root.app.reviewClose("quit") : root.app.reviewCloseWindow(root.shellWindow)
            if (rows.length > 0) {
                closeReview.review(rows)
                return false
            }
            root.admittingClose = true
            root.app.finishClose()
            root.admittingClose = false
            if (!root.app.closeApproved(root.shellWindow))
                return false
        }
        if (root.shellWindow === 0 && !root.app.quitApproved) {
            root.app.hideFirstWindow()
            return false
        }
        return true
    }
    onClosing: close => {
        if (root.shellWindow !== 0) {
            if (!root.app.quitApproved)
                root.app.shellWindowClosed(root.shellWindow)
            return
        }
        if (!root.app.quitApproved) {
            const rows = root.app.reviewClose("quit")
            if (rows.length > 0) {
                close.accepted = false
                closeReview.review(rows)
                return
            }
        }
        searchTool.save()
        root.app.endSession()
    }
    Connections {
        target: StatusHelp
        function onShown(text) { root.shell.statusText = text }
        function onCleared() { root.shell.statusText = "" }
    }
    Connections {
        target: root.app
        function onCloseFinished(done, problem) {
            if (done) {
                closeReview.close()
                if (root.app.closeApproved(root.shellWindow) && !root.admittingClose)
                    root.close()
            } else {
                closeReview.fail(problem)
            }
        }
        function onFindOpenReview(rows) {
            closeReview.review(rows)
        }
        function onFindReplaceDestroyed() {
            searchTool.destroyTool()
            searchDock.close()
        }
    }

    function fitAudioBox() {
        const chrome = audioDock.isOpen ? Math.round(root.workspaceLayout.panelSize(audioDock).height - audioPanel.bodyHeight) : 0
        // KDDW: a separator dragged past the editor's minimum pushes the next one, so the minimum is its rows' height.
        Docking.setMinimumSize(root.engineName("Editor"), Qt.size(editorPanel.minimumSize.width,
                                                 Math.ceil(editorColumn.implicitHeight) + 8))
        for (let pass = 0; pass < 3; ++pass) {
            const body = root.workspaceLayout.panelSize(audioDock).height - chrome
            // MUST KILL: 170 is a magic audio box body height
            const deficit = Math.round(170 - body)
            if (deficit === 0 || !audioDock.isOpen)
                break
            Docking.resizeInLayout(root.engineName("Audio"), 0, 0, 0, deficit)
        }
        editorPanel.reportMinimumSize()
    }

    function defaultLayout() {
        dockingArea.addDockWidget(gridDock, KDDW.KDDockWidgets.Location_OnBottom)
        dockingArea.addDockWidget(videoDock, KDDW.KDDockWidgets.Location_OnTop, gridDock, Qt.size(640, 520))
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnRight, videoDock)
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 272))
        dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        originalDock.close()
        dockingArea.addDockWidget(postprocessorDock, KDDW.KDDockWidgets.Location_OnRight, audioDock, Qt.size(360, 0))
        postprocessorDock.close()
        dockingArea.addDockWidget(referenceDock, KDDW.KDDockWidgets.Location_OnBottom, gridDock, Qt.size(0, 160))
        if (!root.shell.hasReference)
            referenceDock.close()
        editorDock.addDockWidgetAsTab(stylesDock)
        editorDock.addDockWidgetAsTab(templateDock)
        editorDock.setAsCurrentTab()
        stylesDock.close()
        templateDock.close()
    }

    function nodeEffectsLayout() {
        for (const dock of [gridDock, editorDock, audioDock, originalDock, referenceDock, postprocessorDock, stylesDock,
                            templateDock, timingDock, searchDock, automationDock, spellDock, logDock])
            if (dock.isOpen)
                dock.close()
        const width = dockingArea.width
        dockingArea.addDockWidget(nodeGraphDock, KDDW.KDDockWidgets.Location_OnRight, videoDock,
                                  Qt.size(Math.round(width * 0.44), 0))
        dockingArea.addDockWidget(nodeInspectorDock, KDDW.KDDockWidgets.Location_OnRight, nodeGraphDock,
                                  Qt.size(Math.round(width * 0.22), 0))
        dockingArea.addDockWidget(nodeSourcesDock, KDDW.KDDockWidgets.Location_OnBottom, videoDock,
                                  Qt.size(0, Math.round(dockingArea.height * 0.42)))
    }
    function setNodeEffects(on) {
        if (on === root.workspaceLayout.nodeEffects)
            return
        root.nodeEffects.commit()
        if (on)
            root.workspaceLayout.enterNodeEffects()
        else
            root.workspaceLayout.leaveNodeEffects()
        if (on)
            root.focusPanel(nodeGraphPanel, Qt.OtherFocusReason)
    }

    function typesettingLayout() {
        const area = root.workspaceLayout.panelSize(gridDock).width
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnLeft, gridDock,
                                  Qt.size(Math.round(area * 0.44), 0))
        const fieldsShort = Math.ceil(editorInspector.wideMinimum - editorColumn.width)
        const room = Math.round(area * 0.5) - root.workspaceLayout.panelSize(editorDock).width
        if (fieldsShort > 0 && room > 0)
            Docking.resizeInLayout(root.engineName("Editor"), 0, 0, Math.min(fieldsShort, room), 0)
        const original = originalDock.isOpen
        originalDock.open()
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 200))
        dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        audioDock.close()
        if (!original)
            originalDock.close()
        dockingArea.addDockWidget(stylesDock, KDDW.KDDockWidgets.Location_OnRight, videoDock,
                                  Qt.size(Math.round(area * 0.24), 0))
        const tools = stylesTool.toolbar
        const short = Math.ceil(tools.implicitWidth + 12 - tools.width)
        if (tools.visible && short > 0)
            Docking.resizeInLayout(root.engineName("Styles"), short, 0, 0, 0)
        // KDDW doesn't move the closed Audio/Original places' edge, so the Video panel's bottom edge moves instead.
        editorColumn.ensurePolished()
        const lower = root.workspaceLayout.panelSize(editorDock).height
        const natural = Math.ceil(editorColumn.implicitHeight) + 8 + lower - editorPanel.height
        if (lower !== natural)
            Docking.resizeInLayout(root.engineName("Video"), 0, 0, 0, lower - natural)
    }
    function timingLayout() {
        videoDock.close()
        postprocessorDock.open()
    }
    function karaokeLayout() {
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, gridDock,
                                  Qt.size(0, root.workspaceLayout.panelSize(audioDock).height))
        templateDock.open()
        templateDock.raise()
        const lower = root.workspaceLayout.panelSize(gridDock).height
        const short = Math.ceil(lower - grid.height + grid.rowHeight * 8)
        if (short !== lower)
            Docking.resizeInLayout(root.engineName("Grid"), 0, short - lower, 0, 0)
    }
    Connections {
        target: root.workspaceLayout
        function onBuildArrangement(name) {
            if (name === "Timing")
                root.timingLayout()
            else if (name === "Typesetting")
                root.typesettingLayout()
            else if (name === "Karaoke")
                root.karaokeLayout()
            else if (name === "NodeEffects")
                root.nodeEffectsLayout()
        }
    }
    Connections {
        target: root.shell
        function onHasReferenceChanged() {
            if (root.shell.hasReference && gridDock.isOpen)
                referenceDock.open()
            else if (referenceDock.isOpen)
                referenceDock.close()
        }
    }

    readonly property bool originalShown: root.editor.translationMode && originalDock.isOpen
    readonly property bool translationMode: root.editor.translationMode
    onTranslationModeChanged: root.placeOriginal()
    function placeOriginal() {
        if (!root.arrangementSettled)
            return
        if (!root.editor.translationMode) {
            if (originalDock.isOpen)
                originalDock.close()
        } else if (!originalDock.isOpen) {
            root.openOriginal()
        }
    }
    function openOriginal() {
        if (Docking.hasPlace(root.engineName("Original")))
            originalDock.open()
        else
            root.addOriginal()
    }
    function addOriginal() {
        if (editorDock.isOpen && editorDock.isFloating)
            editorDock.addDockWidgetToContainingWindow(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock,
                                                       Qt.size(0, 140))
        else if (editorDock.isOpen)
            dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        else
            dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(400, 0))
    }

    function addLog() {
        dockingArea.addDockWidget(logDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 180))
    }
    function addSpellChecker() {
        dockingArea.addDockWidget(spellDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 180))
    }
    function openSpellChecker() {
        const checking = spellTool.checking
        root.showPanel(spellDock)
        if (checking)
            spellTool.start()
    }

    function addStyles() {
        if (editorDock.isOpen)
            editorDock.addDockWidgetAsTab(stylesDock)
        else
            dockingArea.addDockWidget(stylesDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(300, 0))
    }
    function addTemplate() {
        if (editorDock.isOpen)
            editorDock.addDockWidgetAsTab(templateDock)
        else
            dockingArea.addDockWidget(templateDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(300, 0))
    }

    function addAgent() {
        if (reviewDock.isOpen && !reviewDock.isFloating)
            reviewDock.addDockWidgetAsTab(agentDock)
        else
            root.addRightOfGrid(agentDock, 446)
    }

    function addRightOfGrid(dock, preferredWidth) {
        const grid = gridDock.isOpen && !gridDock.isFloating
        const left = Docking.placeInLayout(root.engineName("Grid")).x
        const column = root.dockList.find(d => d.uniqueName === Docking.dockRightOf(root.engineName("Grid")))
        if (column) {
            column.addDockWidgetAsTab(dock)
        } else {
            const third = Math.round((grid ? root.workspaceLayout.panelSize(gridDock).width : dockingArea.width) / 3)
            const width = Math.min(Math.max(Docking.minimumSize(dock.uniqueName).width, preferredWidth ?? 0), third)
            dockingArea.addDockWidget(dock, KDDW.KDDockWidgets.Location_OnRight, grid ? gridDock : null, Qt.size(width, 0))
        }
        const moved = Docking.placeInLayout(root.engineName("Grid")).x - left
        if (grid && moved !== 0)
            Docking.resizeInLayout(root.engineName("Grid"), moved, 0, 0, 0)
    }

    function openAutomationRun(runId) {
        if (root.videoFullscreen.active)
            root.videoFullscreen.leave()
        root.showPanel(automationDock)
        automationTool.showRun(runId)
    }

    readonly property list<Item> builtInPanels: [videoPanel, audioPanel, originalPanel, editorPanel, gridPanel, referencePanel,
                                                 timingPanel, postprocessorPanel, searchPanel, automationPanel, spellPanel,
                                                 logPanel, stylesPanel, templatePanel, qcNotesPanel, reviewPanel,
                                                 agentPanel, nodeGraphPanel, nodeInspectorPanel, nodeSourcesPanel]
    readonly property var builtInDocks: [videoDock, audioDock, originalDock, editorDock, gridDock, referenceDock, timingDock,
                                         postprocessorDock, searchDock, automationDock, spellDock, logDock, stylesDock,
                                         templateDock, qcNotesDock].concat(root.review.on ? [reviewDock] : [])
                                         .concat(root.agent.on ? [agentDock] : [])
                                         .concat([nodeGraphDock, nodeInspectorDock, nodeSourcesDock])
    property var pluginDocks: []
    readonly property var panels: Array.from(builtInPanels).concat(pluginDocks.map(d => d.panel))
    readonly property var dockList: builtInDocks.concat(pluginDocks)
    readonly property var pluginUi: root.app.pluginRuntime ? root.app.pluginRuntime.ui : null
    readonly property var pluginPanelRows: {
        const rows = root.pluginUi ? root.pluginUi.panels.slice() : []
        for (const dock of root.pluginDocks)
            if (!rows.some(r => r.dock === dock.storedName))
                rows.push({ dock: dock.storedName, title: dock.title, pluginName: dock.pluginName, area: "right" })
        return rows
    }
    function isPluginDock(dock) { return !!dock && dock.isPlugin === true }
    function pluginDock(name) {
        const known = root.pluginDocks.find(d => d.storedName === name)
        if (known || !root.workspaceLayout.isPluginPanel(name))
            return known ?? null
        const dock = pluginDockComponent.createObject(dockingArea, { uniqueName: root.engineName(name), storedName: name })
        dock.openedWithoutPlace.connect(() => root.openAtFirstPlace(dock))
        dock.isOpenChanged.connect(() => root.app.apiHost.panelChanged(name))
        dock.isFloatingChanged.connect(() => root.app.apiHost.panelChanged(name))
        keyRouter.addPanel(dock.panel, null)
        root.pluginDocks = root.pluginDocks.concat([dock])
        return dock
    }
    function showPluginPanel(name) {
        const dock = root.pluginDock(name)
        if (dock)
            root.showPanel(dock)
    }
    function addPluginDock(dock) {
        const row = root.pluginUi ? root.pluginUi.panels.find(r => r.dock === dock.storedName) : undefined
        const area = row ? row.area : "right"
        if (area === "bottom" && logDock.isOpen && !logDock.isFloating)
            logDock.addDockWidgetAsTab(dock)
        else if (area === "bottom")
            dockingArea.addDockWidget(dock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 180))
        else if (area === "left")
            dockingArea.addDockWidget(dock, KDDW.KDDockWidgets.Location_OnLeft, null, Qt.size(300, 0))
        else
            root.addRightOfGrid(dock, 300)
    }
    function forgetPluginDock(dock) {
        dock.forceClose()
        root.pluginDocks = root.pluginDocks.filter(d => d !== dock)
        dock.deleteDockWidgetLater()
    }
    Connections {
        target: root.workspaceLayout
        function onPluginDocksNeeded(docks) {
            for (const name of docks)
                root.pluginDock(name)
        }
    }
    Connections {
        target: root.pluginUi
        function onShowRequested(dock) { root.showPluginPanel(dock) }
        function onDialogOpened(state) {
            const window = pluginDialogComponent.createObject(root, { dialogState: state })
            root.pluginDialogs[state.key] = window
            window.open()
        }
        function onDialogClosed(state) {
            const window = root.pluginDialogs[state.key]
            delete root.pluginDialogs[state.key]
            if (!window)
                return
            window.finish()
            window.destroy()
        }
    }
    property var pluginDialogs: ({})
    Component {
        id: pluginDialogComponent
        PluginDialog {
            ui: root.pluginUi
            settings: root.app.settings
            colourPicker: root.colourPicker
        }
    }
    Component {
        id: pluginDockComponent
        PanelDock {
            id: pluginDock
            objectName: "pluginDock_" + uniqueName
            affinities: root.dockAffinities
            readonly property bool isPlugin: true
            required property string storedName
            readonly property alias panel: pluginPanel
            readonly property alias body: pluginBody
            property string pluginName
            property string knownTitle
            title: knownTitle || storedName
            readonly property string tip: pluginName.length > 0
                                          ? qsTranslate("HikariSub.Plugins", "%1, from %2").arg(title, pluginName) : ""
            onTipChanged: Docking.setPanelTip(uniqueName, tip)
            Component.onCompleted: Docking.setPanelTip(uniqueName, tip)
            function remember() {
                if (knownTitle.length > 0)
                    root.workspaceLayout.rememberPluginPanel(storedName, knownTitle, pluginName)
            }
            onKnownTitleChanged: remember()
            onPluginNameChanged: remember()
            Panel {
                id: pluginPanel
                objectName: "pluginPanel_" + pluginDock.uniqueName
                dockName: pluginDock.uniqueName
                minimumBodySize: pluginBody.minimumBodySize
                anchors.fill: parent
                title: pluginDock.title
                PluginPanelBody {
                    id: pluginBody
                    objectName: "pluginPanelBody_" + pluginDock.uniqueName
                    anchors.fill: parent
                    ui: root.pluginUi
                    dock: pluginDock.storedName
                    saved: root.workspaceLayout.pluginPanelInfo(pluginDock.storedName)
                    shown: pluginDock.isOpen
                    colourPicker: root.colourPicker
                    onOpenPlugins: plugin => pluginManagerDialog.showPlugin(plugin)
                    onTurnOn: plugin => root.app.pluginManager.setEnabled(plugin, true)
                    onRestart: plugin => root.app.pluginRuntime.restart(plugin)
                    onDetails: root.showPanel(logDock)
                    onCopy: text => root.app.copyText(text)
                    // KDDW deletes these children inside ~DockWidget; a binding on pluginBody would then call into the dying dock.
                    onPluginNameChanged: pluginDock.pluginName = pluginName
                    onTitleChanged: pluginDock.knownTitle = title
                    Component.onCompleted: {
                        pluginDock.pluginName = pluginName
                        pluginDock.knownTitle = title
                    }
                }
            }
        }
    }
    Instantiator {
        model: root.builtInDocks
        delegate: Connections {
            required property var modelData
            target: modelData
            function onOpenedWithoutPlace() { root.openAtFirstPlace(modelData) }
        }
    }
    WheelScroll {}
    KeyRouter {
        id: keyRouter
        objectName: "keyRouter"
        fallbackTarget: videoPanel
        fallbackAccepts: (key, modifiers) => root.hotkeys.actionFor(3, key, modifiers) === "VIDEO_PLAY_PAUSE"
        followsPointer: () => root.app.settings.value("audio.autoFocus") && !root.audio.tapOn
        onFocusRequested: panel => root.focusPanel(panel, Qt.OtherFocusReason)
        Component.onCompleted: {
            for (const panel of root.panels)
                keyRouter.addPanel(panel, panel.keyTarget)
        }
    }

    readonly property bool layoutLocked: root.workspaceLayout.locked
    readonly property bool panelFloats: root.dockList.some(dock => dock.isOpen && dock.isFloating)
    readonly property string lockReason: !root.layoutLocked && root.panelFloats
        ? qsTranslate("HikariSub.Workspace", "Dock floating panels to lock the layout") : ""
    property bool lockRefusalShown: false
    Action {
        id: lockLayoutAction
        text: qsTranslate("HikariSub.Workspace", "Lock layout")
        checkable: true
        checked: root.layoutLocked
        enabled: root.layoutLocked || !root.panelFloats
        onTriggered: {
            if (!root.hotkeyGesture("GLOBAL_TOGGLE_LOCK_LAYOUT"))
                root.workspaceLayout.setLocked(!root.layoutLocked)
            checked = Qt.binding(() => root.layoutLocked)
        }
    }
    Connections {
        target: Docking
        function onLayoutChangeRefused() {
            if (root.lockRefusalShown)
                return
            root.lockRefusalShown = true
            root.notify("info", qsTranslate("HikariSub.Workspace", "The layout is locked"),
                        qsTranslate("HikariSub.Workspace", "Unlock it to move panels. Resizing still works."),
                        { actions: [{ text: qsTranslate("HikariSub.Workspace", "Unlock"),
                                      run: () => root.workspaceLayout.setLocked(false) }] })
        }
    }

    function applyPreset(name) {
        if (root.layoutLocked)
            return
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applyPreset(name))
        root.analytics.layoutChanged(name)
    }
    function applySavedLayout(name) {
        if (root.layoutLocked)
            return
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applySavedLayout(name))
        root.analytics.layoutChanged("saved")
    }
    function resetLayout() {
        if (root.layoutLocked)
            return
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.resetCurrentLayout())
    }
    function keepFocusShown(apply) {
        const focused = root.panels.find(p => p.visible && p.activeFocus) ?? null
        apply()
        if (!focused || focused.visible)
            return
        const shown = root.panels.filter(p => p.visible)
        const next = shown.includes(gridPanel) ? gridPanel : shown[0]
        if (next)
            root.focusPanel(next, Qt.OtherFocusReason)
    }
    Connections {
        target: root.workspaceLayout
        function onArrangementSwapped() {
            if (root.shell.hasReference && gridDock.isOpen)
                referenceDock.open()
            else if (referenceDock.isOpen)
                referenceDock.close()
            if (!videoDock.isOpen && root.video.playing)
                root.video.pause()
            // MUST KILL: deferred because the arrangement swaps in before the editor leaves the old tab
            Qt.callLater(root.placeOriginal)
        }
        function onPresetApplied(name) {
            if (name === "Timing" && root.shiftTimes.timingPreset !== "")
                root.shiftTimes.applyPreset("postprocessor", root.shiftTimes.timingPreset)
        }
        function onPresetModeChosen(mode) {
            if (mode === "Translation" && root.app.translationQuestionDue())
                root.openInMainWindow(translationModeQuestion)
            if (mode === "Karaoke") {
                const karaoke = root.audio.karaoke
                const folds = root.shell.foldGenerated
                root.audio.setKaraoke(true)
                root.shell.foldGenerated = true
                root.karaokeTurnedOn = root.karaokeTurnedOn || !karaoke
                root.foldTurnedOn = root.foldTurnedOn || !folds
            }
        }
        function onPresetModeLeft(mode) {
            if (mode !== "Karaoke")
                return
            if (root.karaokeTurnedOn)
                root.audio.setKaraoke(false)
            if (root.foldTurnedOn)
                root.shell.foldGenerated = false
            root.karaokeTurnedOn = false
            root.foldTurnedOn = false
        }
    }
    property bool karaokeTurnedOn: false
    property bool foldTurnedOn: false
    readonly property bool audioKaraoke: root.audio.karaoke
    readonly property bool gridFolds: root.shell.foldGenerated
    onAudioKaraokeChanged: root.karaokeTurnedOn = false
    onGridFoldsChanged: root.foldTurnedOn = false
    function keepKaraokeTurnedOn() {
        root.app.settings.setValue("workspace.karaokeTurnedOn",
                                   (root.karaokeTurnedOn ? ["audio.karaoke"] : []).concat(root.foldTurnedOn ? ["grid.foldGenerated"] : []))
    }
    onKaraokeTurnedOnChanged: root.keepKaraokeTurnedOn()
    onFoldTurnedOnChanged: root.keepKaraokeTurnedOn()

    function openFind(withReplace) {
        if (searchDock.isOpen && searchPanel.activeFocus && (!withReplace || searchTool.replaceShown)) {
            searchDock.close()
            root.focusPanel(editorPanel, Qt.OtherFocusReason)
            return
        }
        searchTool.open(withReplace)
        root.showPanel(searchDock)
        searchTool.focusFind()
    }

    function applyShift(tool) {
        const panel = tool === "postprocessor" ? postprocessorTool : shiftTimesPanel
        panel.message = root.app.applyShift(tool)
        panel.refresh()
    }

    function runTimingHelper(symbol) {
        const app = root.app
        const helpers = {
            GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME: () => app.snapToAdjacentKeyframe(false, false),
            GLOBAL_SNAP_START_TO_NEXT_KEYFRAME: () => app.snapToAdjacentKeyframe(false, true),
            GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME: () => app.snapToAdjacentKeyframe(true, false),
            GLOBAL_SNAP_END_TO_NEXT_KEYFRAME: () => app.snapToAdjacentKeyframe(true, true),
            GLOBAL_ADD_LEAD_IN: () => app.changeLead(false, true),
            GLOBAL_REMOVE_LEAD_IN: () => app.changeLead(false, false),
            GLOBAL_ADD_LEAD_OUT: () => app.changeLead(true, true),
            GLOBAL_REMOVE_LEAD_OUT: () => app.changeLead(true, false),
            GLOBAL_START_FRAME_EARLIER: () => app.nudgeEdge(false, -1),
            GLOBAL_START_FRAME_LATER: () => app.nudgeEdge(false, 1),
            GLOBAL_END_FRAME_EARLIER: () => app.nudgeEdge(true, -1),
            GLOBAL_END_FRAME_LATER: () => app.nudgeEdge(true, 1),
            GLOBAL_EXTEND_PREVIOUS_TO_START: () => app.extendNeighbour(false),
            GLOBAL_EXTEND_NEXT_TO_END: () => app.extendNeighbour(true),
            GLOBAL_TIMING_ASSISTANT: () => {
                postprocessorTool.assistant = true
                root.showPanel(postprocessorDock)
                postprocessorTool.focusAssistant()
                return true
            },
            GLOBAL_TIMING_ASSISTANT_ACCEPT: () => app.acceptTimingSuggestion(root.shellWindow, -1, -1),
        }
        if (!helpers[symbol]())
            root.notices.post("info", qsTr("No line changed"), "")
        postprocessorTool.schedule()
    }

    function focusedDock() {
        const w = root.workspaceLayout.focusWindow
        const i = panels.findIndex(p => p.activeFocus && p.Window.window === w)
        return i >= 0 ? dockList[i] : gridDock
    }

    // MUST KILL: kind is a magic int (0 tab, 1 left, 2 above, 3 right, 4 below, 5 float)
    function placePanel(dock, kind, target) {
        if (!root.panelAvailable(dock))
            return false
        if (kind === 5) {
            if (!dock.isFloating)
                root.setPanelFloating(dock, true)
        } else {
            if (!target || target === dock || !target.isOpen)
                return false
            if (kind === 0) {
                target.addDockWidgetAsTab(dock)
            } else {
                const location = [0, KDDW.KDDockWidgets.Location_OnLeft, KDDW.KDDockWidgets.Location_OnTop,
                                  KDDW.KDDockWidgets.Location_OnRight, KDDW.KDDockWidgets.Location_OnBottom][kind]
                if (target.isFloating)
                    target.addDockWidgetToContainingWindow(dock, location, target)
                else
                    dockingArea.addDockWidget(dock, location, target)
            }
        }
        root.showPanel(dock)
        return true
    }

    function moveTab(dock, step) {
        if (!Docking.moveTab(dock.uniqueName, step))
            return false
        root.showPanel(dock)
        return true
    }

    function openEditorFromFullScreen() {
        if (root.app.openEditorFromFullScreen())
            root.showPanel(editorDock)
    }
    function panelAvailable(dock) {
        return dock !== originalDock || root.editor.translationMode
    }
    function stepQcNote(forward) {
        if (!qcNotesTool.step(forward) && !qcNotesDock.isOpen)
            root.notices.post("info", qsTranslate("HikariSub.QcNotes", "QC notes"), qsTranslate("HikariSub.QcNotes", "No more notes"))
    }
    function addQcNote(ms) {
        if (!root.qcNotes.editable) {
            if (root.qcNotes.hasDocument && !root.qcNotes.canKeepNotes)
                root.showPanel(qcNotesDock)
            return
        }
        root.showPanel(qcNotesDock)
        qcNotesTool.startAdding(ms)
    }
    function showPanel(dock) {
        if (!root.panelAvailable(dock))
            return
        root.analytics.panelOpened(root.panelName(dock))
        root.openAtFirstPlace(dock)
        root.workspaceLayout.keepFloatingPanelsOnScreen()
        dock.raise()
        if (dock === spellDock && !spellTool.checking)
            spellTool.start()
        root.focusPanel(panels[dockList.indexOf(dock)], Qt.OtherFocusReason)
    }
    function openAtFirstPlace(dock) {
        if (dock === originalDock)
            root.openOriginal()
        else if (dock === logDock && !Docking.hasPlace(root.engineName("Log")))
            root.addLog()
        else if (dock === spellDock && !Docking.hasPlace(root.engineName("Spellchecker")))
            root.addSpellChecker()
        else if (dock === stylesDock && !Docking.hasPlace(root.engineName("Styles")))
            root.addStyles()
        else if (dock === templateDock && !Docking.hasPlace(root.engineName("Template")))
            root.addTemplate()
        else if (dock === agentDock && !Docking.hasPlace(root.engineName("Agent")))
            root.addAgent()
        else if (root.isPluginDock(dock) && !Docking.hasPlace(dock.uniqueName))
            root.addPluginDock(dock)
        else if (!Docking.hasPlace(dock.uniqueName))
            root.addRightOfGrid(dock)
        else
            dock.open()
        if (root.layoutLocked && dock.isFloating)
            dock.isFloating = false
    }

    function panelOf(item) {
        for (let p = item; p; p = p.parent) {
            for (const panel of panels)
                if (p === panel)
                    return panel
            if (p.objectName === "dockHeader") {
                const i = dockList.findIndex(d => d.uniqueName === p.currentName)
                if (i >= 0)
                    return panels[i]
            }
        }
        return null
    }

    function openUserGuide() {
        const w = root.workspaceLayout.focusWindow
        if (w && w.guideId !== undefined) {
            UserGuide.open("window", w.guideId)
            return
        }
        const hovered = keyRouter.followsPointer() ? keyRouter.hoveredPanel : null
        const panel = hovered || root.panelOf(w ? w.activeFocusItem : root.activeFocusItem)
        const i = panel ? root.panels.indexOf(panel) : -1
        UserGuide.open(i >= 0 ? "panel" : "", i >= 0 ? (panel.guideId || root.panelName(root.dockList[i])) : "")
    }
    Connections {
        target: UserGuide
        enabled: root.shellWindow === 0
        function onOpenRequested(kind, id) { root.app.openUserGuide(kind, id) }
    }
    Shortcut {
        objectName: "dialogUserGuide"
        sequences: root.hotkeys.globalShortcuts.filter(s => s.symbol === "GLOBAL_HELP").map(s => s.keys)
        context: Qt.ApplicationShortcut
        enabled: root.visible && !root.shellActive && root.workspaceLayout.focusWindow !== null
                 && root.workspaceLayout.focusWindow.guideId !== undefined
        onActivated: root.openUserGuide()
    }

    // Item flags are functions: a new model list makes the Instantiator remake every entry, the open one too.
    function panelOwnItems(dock) {
        if (dock === editorDock)
            return [{ name: "showTimes", text: qsTranslate("HikariSub.Editor", "Show times"), checkable: true, radio: true,
                      checked: () => !root.editor.showFrames, enabled: () => root.editor.framesAvailable,
                      run: () => root.editor.showFrames = false },
                    { name: "showFrames", text: qsTranslate("HikariSub.Editor", "Show frames"), checkable: true, radio: true,
                      checked: () => root.editor.showFrames, enabled: () => root.editor.framesAvailable,
                      run: () => root.editor.showFrames = true }]
        if (dock === referenceDock)
            return [{ name: "compareWithReference", text: qsTranslate("HikariSub.Reference", "Compare with reference"),
                      enabled: () => root.shell.hasEditingTarget, run: () => root.app.compareWithReference(), iconRole: "compare" }]
        if (dock === automationDock)
            return [{ name: "automationLoadScript", text: qsTranslate("HikariSub.Automation", "Load script…"), iconRole: "automation",
                      enabled: () => loadScriptAction.enabled, run: () => loadScriptAction.trigger() },
                    { name: "automationRefreshAutoload", text: qsTranslate("HikariSub.Automation", "Refresh autoload scripts"),
                      iconRole: "refresh", enabled: () => reloadAutoloadAction.enabled && !root.automation.running,
                      run: () => reloadAutoloadAction.trigger() },
                    { name: "automationShortcutsInSettings", text: qsTranslate("HikariSub.Automation", "Shortcuts in Settings…"),
                      iconRole: "hotkeys", run: () => root.openSettings("hotkeys", 7) }]
        if (dock === spellDock)
            return [{ name: "spellIgnoreComments", text: qsTranslate("HikariSub.Spelling", "Ignore comments"), checkable: true,
                      checked: () => spellTool.ignoreComments, run: () => spellTool.setIgnoreComments(!spellTool.ignoreComments) },
                    { name: "spellIgnoreUpper", text: qsTranslate("HikariSub.Spelling", "Ignore words in capitals"), checkable: true,
                      checked: () => spellTool.ignoreUpperCase,
                      run: () => spellTool.setIgnoreUpperCase(!spellTool.ignoreUpperCase) },
                    { name: "spellAddedWords", text: qsTranslate("HikariSub.Spelling", "Words added to dictionary…"),
                      iconRole: "dictionary", run: () => spellTool.openAddedWords() }]
        if (dock === gridDock)
            return [{ name: "ignoreFilteringInActions", text: qsTranslate("HikariSub.Grid", "Ignore filtering in some actions"),
                      command: "GRID_FILTER_IGNORE_IN_ACTIONS", checkable: true,
                      checked: () => root.gridFilter.ignoreInActions,
                      run: () => root.gridFilter.ignoreInActions = !root.gridFilter.ignoreInActions },
                    { name: "foldGenerated", text: qsTranslate("HikariSub.Grid", "Fold generated lines"), checkable: true,
                      checked: () => root.shell.foldGenerated,
                      run: () => root.shell.foldGenerated = !root.shell.foldGenerated }]
        if (dock === qcNotesDock)
            return [{ name: "qcNotesImport", text: qsTranslate("HikariSub.QcNotes", "Import notes file…"), iconRole: "import",
                      enabled: () => root.qcNotes.editable, run: () => qcNotesTool.openImport() },
                    { name: "qcNotesImportClipboard", text: qsTranslate("HikariSub.QcNotes", "Import notes from the clipboard"),
                      iconRole: "edit-paste", enabled: () => root.qcNotes.editable && root.qcNotes.clipboardHasNotes,
                      run: () => qcNotesTool.importClipboard() },
                    { name: "qcNotesExport", text: qsTranslate("HikariSub.QcNotes", "Export notes…"), iconRole: "export",
                      enabled: () => root.qcNotes.total > 0, run: () => qcNotesTool.openExport() },
                    { name: "qcNotesMarkAllNotDone", text: qsTranslate("HikariSub.QcNotes", "Mark all as not done"), iconRole: "qc-done-reset",
                      enabled: () => root.qcNotes.editable && root.qcNotes.sections.some(s => s.open < s.count),
                      run: () => root.qcNotes.markAllNotDone() },
                    { name: "qcNotesRemoveAll", text: qsTranslate("HikariSub.QcNotes", "Remove all notes…"), iconRole: "delete",
                      enabled: () => root.qcNotes.editable && root.qcNotes.total > 0, run: () => qcNotesTool.removeAll() }]
        if (root.isPluginDock(dock))
            return root.pluginPanelItems(dock)
        return []
    }
    function panelOwnTail(dock) {
        if (dock === automationDock)
            return [{ name: "automationClearRuns", text: qsTranslate("HikariSub.Automation", "Clear runs"),
                      iconRole: "remove", enabled: () => root.automation.runs.count > (root.automation.running ? 1 : 0),
                      run: () => root.automation.runs.clear() }]
        return []
    }
    function pluginPanelItems(dock) {
        const state = dock.body?.panelState
        if (!state)
            return []
        const row = id => state.menu.find(r => r.id === id) ?? {}
        return state.status === "running" ? state.menu.map(r => ({
            name: "pluginMenu_" + r.id, text: r.text, checkable: r.checked !== undefined,
            checked: () => row(r.id).checked === true, enabled: () => row(r.id).disabled !== true,
            run: () => root.pluginUi.send(dock.storedName, { kind: "menu", widget: r.id })
        })) : []
    }
    readonly property VideoFollowChoices videoFollow: VideoFollowChoices { settings: root.app.settings }
    function panelOwnMenus(dock) {
        if (dock === videoDock)
            return [{ name: "videoSeekOn", text: root.videoFollow.seekName, iconRole: "video-to-start-time",
                      items: root.videoFollow.seekChoices.map((text, i) => ({
                          text: text, checked: () => root.videoFollow.seek === i, run: () => root.videoFollow.setSeek(i) })) },
                    { name: "videoThenPlay", text: root.videoFollow.playName, iconRole: "play-line",
                      items: root.videoFollow.playChoices.map((text, i) => ({
                          text: text, checked: () => root.videoFollow.play === i, run: () => root.videoFollow.setPlay(i) })) }]
        return []
    }
    function closePanel(dock) {
        if (dock === referenceDock)
            root.app.closeReference()
        else if (root.isPluginDock(dock) && dock.body.status === "missing")
            root.forgetPluginDock(dock)
        else
            dock.close()
    }
    Connections {
        target: Docking
        function onMenuRequested(name, anchor) {
            const dock = root.dockList.find(d => d.uniqueName === name)
            if (dock && anchor)
                panelOptionsMenu.openFor(dock, anchor)
        }
    }
    ShellMenu {
        id: panelOptionsMenu
        objectName: "panelOptionsMenu"
        property var dock: null
        readonly property var ownItems: dock ? root.panelOwnItems(dock) : []
        readonly property var ownMenus: dock ? root.panelOwnMenus(dock) : []
        readonly property var ownTail: dock ? root.panelOwnTail(dock) : []
        readonly property bool closable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotClosable) === 0
        readonly property bool dockable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotDockable) === 0
        property int tabIndex: -1
        property int tabCount: 0
        readonly property int leftStep: Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1
        readonly property var hostedMenus: [gridFilteringMenu, gridColumnsMenu]
        function openFor(target, anchor) {
            for (let i = count - 1; i >= 0; --i)
                if (hostedMenus.includes(menuAt(i)))
                    takeMenu(i)
            dock = target
            if (target === gridDock)
                hostedMenus.forEach((menu, i) => insertMenu(i, menu))
            tabIndex = Docking.tabIndex(target.uniqueName)
            tabCount = Docking.tabCount(target.uniqueName)
            margins = anchor.Window.window !== root ? Docking.windowShadow(anchor.Window.window) + 1 : 0
            popup(anchor, 0, anchor.height)
        }
        onAboutToShow: Docking.openMenu = dock ? dock.uniqueName : ""
        onAboutToHide: Docking.openMenu = ""
        Instantiator {
            id: panelOwnItemsInstantiator
            model: panelOptionsMenu.ownItems
            delegate: panelOwnItem
            onObjectAdded: (index, object) => panelOptionsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeItem(object)
        }
        Component {
            id: panelOwnItem
            ShellMenuItem {
                required property var modelData
                objectName: "panelOptions_" + modelData.name
                iconRole: modelData.iconRole ?? ""
                text: modelData.text
                command: modelData.command ?? ""
                checkable: modelData.checkable === true
                checked: typeof modelData.checked === "function" ? modelData.checked() : modelData.checked === true
                keepsMenuOpen: checkable && modelData.radio !== true
                enabled: typeof modelData.enabled === "function" ? modelData.enabled() : modelData.enabled !== false
                onTriggered: {
                    if (!command || !root.hotkeyGesture(command, commandWindow))
                        modelData.run()
                    // Clicking a checkable item assigns checked and breaks its binding; restore it.
                    if (typeof modelData.checked === "function")
                        checked = Qt.binding(() => modelData.checked())
                }
            }
        }
        Instantiator {
            id: panelOwnMenusInstantiator
            model: panelOptionsMenu.ownMenus
            delegate: ShellMenu {
                id: ownMenu
                required property var modelData
                objectName: "panelOptions_" + modelData.name
                iconRole: modelData.iconRole
                title: modelData.text
                Instantiator {
                    model: ownMenu.modelData.items
                    delegate: ShellMenuItem {
                        required property var modelData
                        required property int index
                        objectName: ownMenu.objectName + "_" + index
                        text: modelData.text
                        checkable: true
                        checked: modelData.checked()
                        keepsMenuOpen: false
                        onTriggered: {
                            modelData.run()
                            checked = Qt.binding(() => modelData.checked())
                        }
                    }
                    onObjectAdded: (index, object) => ownMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => ownMenu.removeItem(object)
                }
            }
            onObjectAdded: (index, object) => panelOptionsMenu.insertMenu(panelOwnItemsInstantiator.count + index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeMenu(object)
        }
        MenuSeparator {
            id: panelOwnTailSeparator
            visible: panelOptionsMenu.ownTail.length > 0
            height: panelOptionsMenu.ownTail.length > 0 ? implicitHeight : 0
        }
        Instantiator {
            model: panelOptionsMenu.ownTail
            delegate: panelOwnItem
            onObjectAdded: (index, object) => panelOptionsMenu.insertItem(
                               panelOwnItemsInstantiator.count + panelOwnMenusInstantiator.count + 1 + index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeItem(object)
        }
        MenuSeparator {
            objectName: "panelOptionsSeparator"
            visible: panelOptionsMenu.ownItems.length + panelOptionsMenu.ownMenus.length > 0
            height: visible ? implicitHeight : 0
        }
        ShellMenuItem {
            iconRole: "panel-move"
            objectName: "panelOptionsMove"
            text: qsTranslate("HikariSub.Workspace", "Move panel…")
            enabled: panelOptionsMenu.dockable && !root.layoutLocked
            onTriggered: placementWindow.openFor(panelOptionsMenu.dock)
        }
        ShellMenuItem {
            id: panelOptionsTabLeft
            iconRole: "tab-move-left"
            objectName: "panelOptionsTabLeft"
            text: qsTranslate("HikariSub.Workspace", "Move tab left")
            visible: panelOptionsMenu.tabCount > 1
            height: visible ? implicitHeight : 0
            enabled: !root.layoutLocked && panelOptionsMenu.tabIndex + panelOptionsMenu.leftStep >= 0
                     && panelOptionsMenu.tabIndex + panelOptionsMenu.leftStep < panelOptionsMenu.tabCount
            onTriggered: root.moveTab(panelOptionsMenu.dock, panelOptionsMenu.leftStep)
        }
        ShellMenuItem {
            iconRole: "tab-move-right"
            objectName: "panelOptionsTabRight"
            text: qsTranslate("HikariSub.Workspace", "Move tab right")
            visible: panelOptionsTabLeft.visible
            height: visible ? implicitHeight : 0
            enabled: !root.layoutLocked && panelOptionsMenu.tabIndex - panelOptionsMenu.leftStep >= 0
                     && panelOptionsMenu.tabIndex - panelOptionsMenu.leftStep < panelOptionsMenu.tabCount
            onTriggered: root.moveTab(panelOptionsMenu.dock, -panelOptionsMenu.leftStep)
        }
        ShellMenuItem {
            iconRole: panelOptionsMenu.dock && panelOptionsMenu.dock.isFloating ? "panel-dock" : "panel-float"
            objectName: "panelOptionsFloat"
            text: panelOptionsMenu.dock && panelOptionsMenu.dock.isFloating ? qsTranslate("HikariSub.Workspace", "Dock") : qsTranslate("HikariSub.Workspace", "Float")
            enabled: panelOptionsMenu.dock !== null && !root.layoutLocked
                     && (!panelOptionsMenu.dock.isFloating || panelOptionsMenu.dockable)
            onTriggered: root.setPanelFloating(panelOptionsMenu.dock, !panelOptionsMenu.dock.isFloating)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "panelOptionsLock"
            command: "GLOBAL_TOGGLE_LOCK_LAYOUT"
            iconRole: "layout-lock"
            action: lockLayoutAction
            keepsMenuOpen: false
            reason: root.lockReason
        }
        MenuSeparator {}
        Instantiator {
            id: panelOptionsAbout
            readonly property var panelState: root.isPluginDock(panelOptionsMenu.dock) ? (panelOptionsMenu.dock.body?.panelState ?? null) : null
            model: panelState !== null && panelState.status !== "missing" ? [panelState] : []
            delegate: ShellMenuItem {
                required property var modelData
                iconRole: "about"
                objectName: "panelOptionsAbout"
                text: qsTranslate("HikariSub.Plugins", "About %1…").arg(panelOptionsMenu.dock?.pluginName ?? "")
                onTriggered: pluginManagerDialog.showPlugin(modelData.pluginId)
            }
            onObjectAdded: (index, object) => panelOptionsMenu.insertItem(panelOptionsMenu.count - 2, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeItem(object)
        }
        MenuSeparator {
            visible: panelOptionsAbout.count > 0
            height: panelOptionsAbout.count > 0 ? implicitHeight : 0
        }
        ShellMenuItem {
            iconRole: "tab-close"
            objectName: "panelOptionsClose"
            text: qsTranslate("HikariSub.Workspace", "Close")
            enabled: panelOptionsMenu.closable && !root.layoutLocked
            onTriggered: root.closePanel(panelOptionsMenu.dock)
        }
    }

    component GridColumnsMenu: ShellMenu {
        id: columns
        property string prefix
        Instantiator {
            model: [
                { bit: 1, label: qsTranslate("HikariSub.Grid", "Layer"), ass: true, symbol: "GRID_HIDE_LAYER" },
                { bit: 2, label: qsTranslate("HikariSub.Grid", "Start time"), ass: false, symbol: "GRID_HIDE_START" },
                { bit: 4, label: qsTranslate("HikariSub.Grid", "End time"), ass: false, end: true, symbol: "GRID_HIDE_END" },
                { bit: 16, label: qsTranslate("HikariSub.Grid", "Actor"), ass: true, symbol: "GRID_HIDE_ACTOR" },
                { bit: 8, label: qsTranslate("HikariSub.Grid", "Style"), ass: true, symbol: "GRID_HIDE_STYLE" },
                { bit: 32, label: qsTranslate("HikariSub.Grid", "Left margin"), ass: true, symbol: "GRID_HIDE_MARGINL" },
                { bit: 64, label: qsTranslate("HikariSub.Grid", "Right margin"), ass: true, symbol: "GRID_HIDE_MARGINR" },
                { bit: 128, label: qsTranslate("HikariSub.Grid", "Vertical margin"), ass: true, symbol: "GRID_HIDE_MARGINV" },
                { bit: 256, label: qsTranslate("HikariSub.Grid", "Effect"), ass: true, symbol: "GRID_HIDE_EFFECT" },
                { bit: 512, label: qsTranslate("HikariSub.Grid", "Characters per second"), ass: false, symbol: "GRID_HIDE_CPS" },
                { bit: 8192, label: qsTranslate("HikariSub.Grid", "Line wraps"), ass: false, symbol: "GRID_HIDE_WRAPS" }
            ]
            delegate: ShellMenuItem {
                required property var modelData
                objectName: columns.prefix + "hideColumn" + modelData.bit
                command: modelData.symbol
                text: modelData.label
                checkable: true
                checked: (root.shell.hiddenColumns & modelData.bit) === 0
                enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.end || root.shell.endColumn)
                onTriggered: {
                    if (!root.gridGesture(this, modelData.symbol))
                        root.shell.toggleColumn(modelData.bit)
                    checked = Qt.binding(() => (root.shell.hiddenColumns & modelData.bit) === 0)
                }
            }
            onObjectAdded: (index, object) => columns.insertItem(index, object)
            onObjectRemoved: (index, object) => columns.removeItem(object)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: columns.prefix + "hideEmptyColumns"
            text: qsTranslate("HikariSub.Grid", "Hide empty columns")
            checkable: true
            checked: root.shell.autoHideColumns
            onTriggered: {
                root.shell.autoHideColumns = checked
                checked = Qt.binding(() => root.shell.autoHideColumns)
            }
        }
        ShellMenuItem {
            iconRole: "restore-default"
            objectName: columns.prefix + "resetColumnWidths"
            text: qsTranslate("HikariSub.Grid", "Reset column widths")
            enabled: Object.keys(root.shell.gridColumnWidths).length > 0
            onTriggered: root.shell.gridColumnWidths = {}
        }
    }
    component FilterCheck: ShellMenuItem {
        id: check
        property int bit
        property bool assOnly
        property bool translationOnly
        checkable: true
        enabled: (!assOnly || root.shell.assColumns) && (!translationOnly || root.editor.translationMode)
        checked: (root.gridFilter.filterBy & bit) !== 0
        onTriggered: {
            if (!root.gridGesture(this, command))
                root.gridFilter.setFilterBy(bit, checked)
            checked = Qt.binding(() => (root.gridFilter.filterBy & check.bit) !== 0)
        }
    }
    ShellMenu {
        id: gridFilteringMenu
        objectName: "filteringMenu"
        iconRole: "filter"
        title: qsTranslate("HikariSub.Grid", "Filtering")
        property var styleNames: []
        onAboutToShow: styleNames = root.app.styleNames()
        ShellMenuItem { command: "GRID_FILTER"; iconRole: "filter"; objectName: "filter"; text: qsTranslate("HikariSub.Grid", "Filter"); onTriggered: if (!root.gridGesture(this, "GRID_FILTER")) root.app.filterLines() }
        ShellMenuItem {
            command: "GRID_FILTER_BY_NOTHING"
            iconRole: "filter-off"
            objectName: "turnOffFiltering"
            text: qsTranslate("HikariSub.Grid", "Turn off filtering")
            enabled: root.shell.filtered
            onTriggered: if (!root.gridGesture(this, "GRID_FILTER_BY_NOTHING")) root.app.turnOffFiltering()
        }
        MenuSeparator {}
        FilterCheck {
            objectName: "filterBySelection"; command: "GRID_FILTER_BY_SELECTIONS"; bit: 2
            text: qsTranslate("HikariSub.Grid", "Hide selected lines (filter)")
        }
        FilterCheck {
            objectName: "filterByComments"; command: "GRID_FILTER_BY_DIALOGUES"; bit: 4; assOnly: true
            text: qsTranslate("HikariSub.Grid", "Hide comments")
        }
        ShellMenu {
            id: filterStylesMenu
            objectName: "filterStylesMenu"
            iconRole: "styles"
            title: qsTranslate("HikariSub.Grid", "Hide lines with styles")
            enabled: root.shell.assColumns
            Instantiator {
                model: gridFilteringMenu.styleNames
                delegate: ShellMenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: root.gridFilter.styles.indexOf(modelData) >= 0
                    onTriggered: root.gridFilter.setStyle(modelData, checked)
                }
                onObjectAdded: (index, object) => filterStylesMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => filterStylesMenu.removeItem(object)
            }
        }
        FilterCheck {
            objectName: "filterByUnconfirmed"; command: "GRID_FILTER_BY_DOUBTFUL"; bit: 8; translationOnly: true
            text: qsTranslate("HikariSub.Grid", "Show unconfirmed")
        }
        FilterCheck {
            objectName: "filterByUntranslated"; command: "GRID_FILTER_BY_UNTRANSLATED"; bit: 16; translationOnly: true
            text: qsTranslate("HikariSub.Grid", "Show untranslated")
        }
        MenuSeparator {}
        ShellMenuItem {
            command: "GRID_FILTER_INVERT"
            objectName: "filterInvert"
            text: qsTranslate("HikariSub.Grid", "Reverse filtering")
            checkable: true
            checked: root.gridFilter.inverted
            onTriggered: {
                if (!root.gridGesture(this, "GRID_FILTER_INVERT"))
                    root.gridFilter.inverted = checked
                checked = Qt.binding(() => root.gridFilter.inverted)
            }
        }
        ShellMenuItem {
            command: "GRID_FILTER_AFTER_SUBS_LOAD"
            objectName: "filterAfterLoad"
            text: qsTranslate("HikariSub.Grid", "Filter after loading subtitles")
            checkable: true
            enabled: root.shell.assColumns
            checked: root.gridFilter.afterLoad
            onTriggered: {
                if (!root.gridGesture(this, "GRID_FILTER_AFTER_SUBS_LOAD"))
                    root.gridFilter.afterLoad = checked
                checked = Qt.binding(() => root.gridFilter.afterLoad)
            }
        }
        ShellMenuItem {
            command: "GRID_FILTER_DO_NOT_RESET"
            objectName: "filterDoNotReset"
            text: qsTranslate("HikariSub.Grid", "Do not reset previous filtering")
            checkable: true
            checked: root.gridFilter.addToFilter
            onTriggered: {
                if (!root.gridGesture(this, "GRID_FILTER_DO_NOT_RESET"))
                    root.gridFilter.addToFilter = checked
                checked = Qt.binding(() => root.gridFilter.addToFilter)
            }
        }
    }
    GridColumnsMenu {
        id: gridColumnsMenu
        objectName: "columnsMenu"
        iconRole: "columns"
        title: qsTranslate("HikariSub.Grid", "Columns")
    }

    function cyclePanels(step) {
        if (homePage.visible)
            return homePage.takeFocus()
        const shown = panels.filter(p => p.visible)
        const card = automationProgress.reachable ? automationProgress : null
        if (card && card.activeFocus) {
            card.leave()
            if (shown.length > 0)
                root.focusPanel(shown[step > 0 ? 0 : shown.length - 1], Qt.TabFocusReason)
            return
        }
        if (shown.length === 0) {
            if (card)
                root.enterCard(null)
            return
        }
        // Not Window.active: Qt reports a floating panel's window active whenever its transient parent is.
        const focusWindow = root.workspaceLayout.focusWindow
        let current = shown.findIndex(p => p.activeFocus && p.Window.window === focusWindow)
        if (current < 0 && focusWindow === root)
            current = shown.indexOf(panelOf(root.activeFocusItem))
        if (current < 0 && focusWindow)
            current = shown.indexOf(panelOf(focusWindow.activeFocusItem))
        if (card && current >= 0 && current + step >= shown.length || card && current + step < 0 && current >= 0) {
            root.enterCard(shown[current])
            return
        }
        const next = current < 0 ? (step > 0 ? 0 : shown.length - 1)
                                 : (current + step + shown.length) % shown.length
        root.focusPanel(shown[next], Qt.TabFocusReason)
    }

    property var pendingFocus: null

    function enterCard(from) {
        const w = automationProgress.Window.window
        const take = () => automationProgress.enter(from)
        root.pendingFocus = null
        if (w && root.workspaceLayout.focusWindow !== w) {
            root.pendingFocus = { window: w, focus: take }
            w.raise()
            w.requestActivate()
        }
        take()
    }

    function openInMainWindow(dialog) {
        if (root.workspaceLayout.focusWindow !== root) {
            root.raise()
            root.requestActivate()
        }
        dialog.open()
    }

    // Qt restores each window's own focus item on activation, so activeFocus can't tell which field was edited.
    property Item lastTextField: null
    function editedField() {
        return translationText.visible && root.lastTextField === translationText
               && (lineText.activeFocus || translationText.activeFocus) ? translationText : lineText
    }
    Connections {
        target: root.workspaceLayout
        function onFocusWindowChanged() {
            const pending = root.pendingFocus
            if (!pending || root.workspaceLayout.focusWindow !== pending.window) {
                const w = root.workspaceLayout.focusWindow
                const a = root.activeFocusItem
                if (w === root && (!a || !a.parent || a === root.contentItem)) {
                    const inMain = root.panels.filter(p => p.visible && p.Window.window === root)
                    const next = inMain.includes(gridPanel) ? gridPanel : inMain[0]
                    if (next)
                        next.forceActiveFocus(Qt.ActiveWindowFocusReason)
                }
                return
            }
            root.pendingFocus = null
            const take = pending.focus || (() => pending.panel.forceActiveFocus(pending.reason))
            take()
            // Again after KDDW focuses its own frame on a floating window's activation.
            Qt.callLater(take)
        }
    }

    // Window activation is asynchronous on X11 and Wayland; the panel takes the focus again once it activates.
    function focusPanel(panel, reason) {
        const owner = root.panelOf(panel)
        const dock = owner ? root.dockList[root.panels.indexOf(owner)] : null
        if (dock && dock.isOpen && !owner.visible)
            dock.raise()
        const w = panel.Window.window
        root.pendingFocus = null
        if (w && root.workspaceLayout.focusWindow !== w) {
            root.pendingFocus = { window: w, panel: panel, reason: reason }
            w.raise()
            w.requestActivate()
        }
        panel.forceActiveFocus(reason)
    }

    // Deferred: KDDW reparents the panel into its new window first.
    function setPanelFloating(dock, floating) {
        if (root.layoutLocked && floating)
            return
        const panel = panels[dockList.indexOf(dock)]
        dock.isFloating = floating
        Qt.callLater(() => root.focusPanel(panel, Qt.OtherFocusReason))
    }

    function videoDoubleClick() {
        root.videoFullscreen.toggle(0)
        if (!root.videoFullscreen.active)
            root.app.selectVisibleLineAfterFullScreen()
    }
    function openVideoUnasked(path, fullscreen) {
        const found = root.app.openVideoFile(path)
        if (found.subtitles.length === 0) {
            if (fullscreen)
                root.videoFullscreen.enterWhenShown(path)
            root.app.openVideo(path)
            return
        }
        const result = root.app.reviewOpenWithVideo(found.subtitles, path, fullscreen === true)
        if (!result.ok)
            return
        if (result.rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(result.rows)
    }
    function openInstallLink(id) { pluginManagerDialog.followLink(id) }
    function showFolderPicker() { projectFolderDialog.show() }
    function openSessionFile(path) {
        sessionWindows.load(root.app.fileUrl(path))
    }
    function openSingleVideo(path) {
        if (root.app.settings.value("video.fullScreenOnStart")) {
            root.openVideoUnasked(path, true)
        } else {
            tabCommands.openVideoFile(path)
        }
    }

    readonly property bool floatingPanelActive: {
        const w = root.workspaceLayout.focusWindow
        return w !== null && w !== root && panels.some(p => p.visible && p.Window.window === w)
    }
    readonly property bool menuHasKeys: root.workspaceLayout.menuHasFocus
    readonly property bool videoFullscreenActive: root.workspaceLayout.focusWindow !== null
                                                  && root.workspaceLayout.focusWindow === videoFullscreenWindow
    readonly property bool shellActive: (root.workspaceLayout.focusWindow === root || floatingPanelActive
                                         || videoFullscreenActive) && !menuHasKeys
    Repeater {
        objectName: "globalHotkeys"
        model: root.hotkeys.globalShortcuts
        delegate: Item {
            required property var modelData
            Shortcut {
                objectName: "globalHotkey_" + modelData.symbol
                sequences: [modelData.keys]
                context: Qt.ApplicationShortcut
                enabled: root.shellActive
                onActivated: root.runGlobalHotkey(modelData.symbol)
            }
        }
    }
    Shortcut {
        sequences: ["F6"]
        context: Qt.ApplicationShortcut
        enabled: root.shellActive && !root.hotkeys.globalSequences.includes("F6")
        onActivated: root.cyclePanels(1)
    }
    Shortcut {
        sequences: ["Shift+F6"]
        context: Qt.ApplicationShortcut
        enabled: root.shellActive && !root.hotkeys.globalSequences.includes("Shift+F6")
        onActivated: root.cyclePanels(-1)
    }
    // Windows puts the window in menu mode on an unclaimed Shift+F10, which eats the next keys.
    Shortcut {
        sequences: ["Shift+F10"]
        context: Qt.ApplicationShortcut
        enabled: root.visible && Qt.platform.os === "windows" && !root.hotkeys.globalSequences.includes("Shift+F10")
        onActivated: Docking.requestKeyboardContextMenu()
    }
    function closeTab(index) {
        if (index === root.app.currentTab) {
            root.beginClose("close")
            return
        }
        const rows = root.app.reviewCloseTab(index)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    function closeTabs(index, toTheRight) {
        const rows = root.app.reviewCloseTabs(index, toTheRight)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    SessionWindows {
        id: sessionWindows
        app: root.app
        startsTheSession: root.shellWindow === 0
        review: rows => closeReview.review(rows)
        pickerFolder: picker => root.pickerFolder(picker)
        rememberPickerFolder: (picker, file) => root.rememberPickerFolder(picker, file)
    }
    Connections {
        target: root.app
        function onDocumentWritten(id) {
            matroskaSubtitles.written(id, root.currentTab())
            if (root.savingAway[id] !== true)
                return
            const away = Object.assign({}, root.savingAway)
            delete away[id]
            root.savingAway = away
            root.endDiskChange(id)
        }
        function onDocumentWriteFailed(id) { matroskaSubtitles.writeFailed(id) }
        // MUST KILL: the app ignores Grid scroll reports until scrollRestored(), hiding the model reset's transient 0
        function onTabShown(scroll) {
            if (root.app.lastUsedWindow !== root.shellWindow)
                return
            Qt.callLater(() => {
                grid.contentY = scroll * grid.rowHeight
                root.app.scrollRestored()
                root.checkExternalChange()
            })
        }
    }

    property string titleBarChoice: root.app.settings.value("appearance.titleBar")
    Connections {
        target: root.app.settings
        function onChanged(id) {
            if (id === "appearance.titleBar")
                root.titleBarChoice = root.app.settings.value(id)
        }
    }
    MainWindowFrame {
        id: windowFrame
        window: root
        // MUST KILL: customAvailable is read for its change notification (the desktop changes in tests)
        custom: WindowFrame.customAvailable && !WindowFrame.usesSystemTitleBar(root.titleBarChoice)
    }
    menuBar: MainTitleBar {
        id: titleBar
        frame: windowFrame
        menus: appMenus
        switcher: LayoutSwitcher {
            layouts: root.workspaceLayout
            lockAction: lockLayoutAction
            lockReason: root.lockReason
            Layout.leftMargin: titleBar.switcherGap
            onPresetChosen: name => root.applyPreset(name)
            onSavedLayoutChosen: name => root.applySavedLayout(name)
            onSaveRequested: root.lazy(saveLayoutDialogLoader).show()
            onResetRequested: root.resetLayout()
            onManageRequested: root.lazy(manageLayoutsDialogLoader).show()
        }
        tools: IconToolButton {
            objectName: "nodeEffectsToggle"
            iconRole: "node-editor"
            action: nodeEffectsAction
            tip: nodeEffectsAction.text
            focusPolicy: Qt.NoFocus
            implicitHeight: 24
            Layout.alignment: Qt.AlignVCenter
            Accessible.checkable: true
            Accessible.checked: checked
        }
    }
    MainWindowEdges {
        frame: windowFrame
        parent: root.contentItem.parent
        anchors.fill: parent
        z: 100
    }
    Shortcut {
        objectName: "menusShortcut"
        sequences: ["F10"]
        enabled: !root.menuHasKeys && !root.hotkeys.globalSequences.includes("F10")
        onActivated: titleBar.keyboardMode ? titleBar.leaveKeyboard(true) : titleBar.enterKeyboard()
    }

    TitleBarMenus {
        id: appMenus
        TitleBarMenuItem {
            objectName: "fileMenuBarItem"
            menu: ShellMenu {
                title: qsTr("&File")
                ShellMenuItem {
                    command: "GLOBAL_OPEN_SUBS"
                    help: qsTr("Open subtitle file")
                    iconRole: "open-subtitles"
                    action: Action {
                        id: openAction
                        text: qsTr("&Open…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS")) openDialog.show()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_OPEN_FOLDER"
                    help: qsTr("Open a folder as a project")
                    iconRole: "open-project"
                    objectName: "openFolderMenuItem"
                    action: Action {
                        id: openFolderAction
                        text: qsTr("Open &folder…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_FOLDER")) projectFolderDialog.show()
                    }
                }
                ShellMenu {
                    help: qsTr("Recently opened subtitles")
                    id: recentMenu
                    iconRole: "recent-subtitles"
                    objectName: "recentSubtitlesMenu"
                    title: qsTr("Recently opened &subtitles")
                    property var rows: []
                    onAboutToShow: rows = root.app.recentSubtitles()
                    Instantiator {
                        model: recentMenu.rows
                        delegate: ShellMenuItem {
                            required property var modelData
                            required property int index
                            objectName: "recentSubtitles" + index
                            help: qsTr("Open") + " " + modelData.path
                            text: modelData.label
                            onTriggered: if (!root.app.revealRecent(modelData.path)) root.openSubtitles(modelData.path)
                        }
                        onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                    }
                    ShellMenuItem {
                        text: qsTr("None")
                        enabled: false
                        visible: recentMenu.rows.length === 0
                        height: visible ? implicitHeight : 0
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_REMOVE_SUBS"
                    help: qsTr("Remove subtitles from the editor")
                    iconRole: "close-subtitles"
                    objectName: "newMenuItem"
                    action: Action {
                        id: removeSubsAction
                        text: qsTr("Remove subtitles from the &editor")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_REMOVE_SUBS")) root.beginClose("new")
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_CLOSE_PAGE"
                    iconRole: "tab-close"
                    objectName: "closeMenuItem"
                    action: Action {
                        id: closeAction
                        text: qsTr("&Close")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_PAGE")) root.beginClose("close")
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_CLOSE_PROJECT"
                    help: qsTr("Close this window’s tabs and its project")
                    iconRole: "close-project"
                    objectName: "closeProjectMenuItem"
                    action: Action {
                        id: closeProjectAction
                        text: qsTr("Close pro&ject")
                        enabled: root.projectName.length > 0
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_PROJECT")) root.closeProject()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_OPEN_VIDEO"
                    help: qsTr("Opens video file")
                    iconRole: "open-video"
                    objectName: "openVideoMenuItem"
                    action: Action {
                        id: openVideoAction
                        text: qsTr("Open &video…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.show()
                    }
                }
                ShellMenuItem {
                    command: "GRID_SUBS_FROM_MKV"
                    help: qsTr("Imports a subtitle track from a Matroska file")
                    iconRole: "extract-subtitles"
                    objectName: "importMkvMenuItem"
                    text: qsTr("Import subtitles from MKV…")
                    enabled: root.matroska.available
                    onTriggered: if (!root.gridGesture(this, "GRID_SUBS_FROM_MKV")) matroskaSubtitles.begin()
                }
                ShellMenuItem {
                    command: "GLOBAL_IMPORT_LYRICS"
                    help: qsTr("Turns a lyrics file into lines, in the open file or a new one")
                    iconRole: "import-lyrics"
                    objectName: "importLyricsMenuItem"
                    text: qsTr("Import lyrics…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_IMPORT_LYRICS")) lyricsDialog.show()
                }
                ShellMenuItem {
                    command: "GLOBAL_SAVE_SUBS"
                    help: qsTr("Save current file")
                    iconRole: "save"
                    objectName: "saveMenuItem"
                    action: Action {
                        id: saveAction
                        text: qsTr("&Save")
                        enabled: root.editor.editable
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_SUBS")) root.saveSubtitles()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SAVE_ALL_SUBS"
                    help: qsTr("Save all subtitles")
                    iconRole: "save-all"
                    objectName: "saveAllMenuItem"
                    action: Action {
                        id: saveAllAction
                        text: qsTr("Save &all")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_ALL_SUBS"))
                                return
                            root.saveAllTabs()
                        }
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SAVE_SUBS_AS"
                    help: qsTr("Save as")
                    iconRole: "save-as"
                    objectName: "saveAsMenuItem"
                    action: Action {
                        id: saveAsAction
                        text: qsTr("Save &as…")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_SUBS_AS")) root.openSaveDialog()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_EXPORT_SUBS_AS"
                    help: qsTr("Writes a copy of the document in another format")
                    iconRole: "export"
                    objectName: "exportAsMenuItem"
                    action: Action {
                        id: exportAsAction
                        text: qsTr("&Export as…")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_EXPORT_SUBS_AS")) root.openExportDialog()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SAVE_TRANSLATION"
                    help: qsTr("Save translation")
                    iconRole: "save-translation"
                    objectName: "saveTranslationMenuItem"
                    action: Action {
                        id: saveTranslationAction
                        text: qsTr("Save &translation")
                        enabled: root.shell.hasEditingTarget && root.editor.translationMode
                        onTriggered: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_TRANSLATION"))
                                return
                            if (root.app.turnOffTranslationMode())
                                root.openSaveDialog()
                        }
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SAVE_WITH_VIDEO_NAME"
                    help: qsTr("Save subtitles using the video name")
                    iconRole: "save-with-video-name"
                    objectName: "saveWithVideoNameMenuItem"
                    action: Action {
                        id: saveWithVideoNameAction
                        text: qsTr("Save subtitles using the video name")
                        checkable: true
                        checked: root.app.saveWithVideoName
                        onToggled: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_WITH_VIDEO_NAME"))
                                checked = Qt.binding(() => root.app.saveWithVideoName)
                            else
                                root.app.saveWithVideoName = checked
                        }
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_OPEN_AUTO_SAVE"
                    help: qsTr("Opens the selected autosave from the list")
                    iconRole: "autosave"
                    objectName: "openAutoSaveMenuItem"
                    action: Action {
                        id: openAutoSaveAction
                        text: qsTr("Open autosave…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_AUTO_SAVE")) autosaves.show()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_DELETE_TEMPORARY_FILES"
                    help: qsTr("Opens the temporary file removal window")
                    iconRole: "remove-temporary"
                    objectName: "removeTemporaryMenuItem"
                    action: Action {
                        id: removeTemporaryAction
                        text: qsTr("Remove temporary files")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_DELETE_TEMPORARY_FILES")) autosaves.show(true)
                    }
                }
                ShellMenu {
                    help: qsTr("Last session options")
                    iconRole: "last-session"
                    objectName: "lastSessionMenu"
                    title: qsTr("Last session")
                    ShellMenuItem {
                        command: "GLOBAL_LOAD_LAST_SESSION"
                        help: qsTr("Loads previously loaded files")
                        id: loadLastSessionItem
                        iconRole: "last-session"
                        objectName: "loadLastSessionMenuItem"
                        text: qsTr("Load last session")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_LAST_SESSION")) sessionWindows.load()
                    }
                    ShellMenuItem {
                        command: "GLOBAL_LOAD_EXTERNAL_SESSION"
                        help: qsTr("Loads session from saved session file")
                        id: loadSessionFileItem
                        iconRole: "session-open"
                        objectName: "loadSessionFileMenuItem"
                        text: qsTr("Load session from file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_EXTERNAL_SESSION")) sessionWindows.chooseSessionToLoad()
                    }
                    ShellMenuItem {
                        command: "GLOBAL_SAVE_EXTERNAL_SESSION"
                        help: qsTr("Saves session to file")
                        id: saveSessionFileItem
                        iconRole: "session-save"
                        objectName: "saveSessionFileMenuItem"
                        text: qsTr("Save session to file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_EXTERNAL_SESSION")) sessionWindows.chooseSessionToSave()
                    }
                    ShellMenuItem {
                        help: qsTr("Asks whether to load previously loaded files at program startup")
                        objectName: "askForLastSessionMenuItem"
                        text: qsTr("Ask whether to load the last session at program startup")
                        checkable: true
                        checked: root.app.sessionRestore === 1
                        keepsMenuOpen: false
                        onToggled: root.app.sessionRestore = checked ? 1 : 0
                    }
                    ShellMenuItem {
                        help: qsTr("Loads previously loaded files at program startup")
                        objectName: "loadLastSessionOnStartMenuItem"
                        text: qsTr("Load last session after program start")
                        checkable: true
                        checked: root.app.sessionRestore === 2
                        keepsMenuOpen: false
                        onToggled: root.app.sessionRestore = checked ? 2 : 0
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SETTINGS"
                    help: qsTr("Program settings")
                    iconRole: "settings"
                    objectName: "settingsMenuItem"
                    action: Action {
                        id: settingsAction
                        text: qsTr("&Settings")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SETTINGS")) root.openSettings()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_CLOSE_WINDOW"
                    help: qsTr("Close this window; closing the last window exits the program")
                    iconRole: "window-close"
                    objectName: "closeWindowMenuItem"
                    action: Action {
                        text: qsTr("Close &window")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_WINDOW")) Qt.callLater(root.close)
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_QUIT"
                    help: qsTr("Exit the program")
                    iconRole: "exit"
                    objectName: "exitMenuItem"
                    action: Action {
                        text: qsTr("E&xit")
                        onTriggered: root.beginClose("quit")
                    }
                }
            }
        }
        ShellMenu {
            id: editMenu
            title: qsTr("&Edit")
            ShellMenuItem {
                command: "GLOBAL_UNDO"
                help: qsTr("Undo")
                iconRole: "undo"
                action: Action {
                    id: undoAction
                    text: qsTr("&Undo")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_UNDO")) root.editor.undo()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_REDO"
                help: qsTr("Redo")
                iconRole: "redo"
                action: Action {
                    id: redoAction
                    text: qsTr("&Redo")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_REDO")) root.editor.redo()
                }
            }
            ShellMenu {
                help: qsTr("Sorts all lines in ASS file")
                iconRole: "sort"
                objectName: "sortAllMenu"
                title: qsTr("So&rt all lines")
                enabled: root.editor.editable
                id: sortAllMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "sortAll_" + modelData.key
                        command: modelData.all
                        help: modelData.help
                        text: modelData.label
                        onTriggered: if (!root.hotkeyGesture(modelData.all)) root.app.sortLines(modelData.key, false)
                    }
                    onObjectAdded: (index, object) => sortAllMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortAllMenu.removeItem(object)
                }
            }
            ShellMenu {
                help: qsTr("Sorts selected lines in ASS file")
                iconRole: "sort-selected"
                objectName: "sortSelectedMenu"
                title: qsTr("So&rt selected lines")
                enabled: root.editor.editable
                id: sortSelectedMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "sortSelected_" + modelData.key
                        command: modelData.selected
                        help: modelData.help
                        text: modelData.label
                        onTriggered: if (!root.hotkeyGesture(modelData.selected)) root.app.sortLines(modelData.key, true)
                    }
                    onObjectAdded: (index, object) => sortSelectedMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortSelectedMenu.removeItem(object)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_UNDO_TO_LAST_SAVE"
                help: qsTr("Undo to last save")
                iconRole: "undo-to-last-save"
                objectName: "undoToLastSaveMenuItem"
                action: Action {
                    id: undoToLastSaveAction
                    text: qsTr("Undo to last save")
                    enabled: root.editor.canUndoToLastSave
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_UNDO_TO_LAST_SAVE")) root.editor.undoToLastSave()
                }
            }
            ShellMenuItem {
                id: historyMenuItem
                command: "GLOBAL_HISTORY"
                help: qsTr("History")
                iconRole: "history"
                objectName: "historyMenuItem"
                action: Action {
                    id: historyAction
                    text: qsTr("&History")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HISTORY")) root.lazy(historyWindowLoader).open()
                }
            }
            Instantiator {
                model: root.review.on ? 1 : 0
                delegate: ShellMenu {
                    objectName: "reviewMenu"
                    iconRole: "review-keep"
                    title: qsTranslate("HikariSub.Review", "Re&view")
                    ShellMenuItem { command: "GLOBAL_KEEP_AI_CHANGE"; iconRole: "review-keep"; objectName: "keepAiChangeMenuItem"; action: keepAiChangeAction }
                    ShellMenuItem { command: "GLOBAL_UNDO_AI_CHANGE"; iconRole: "undo"; objectName: "undoAiChangeMenuItem"; action: undoAiChangeAction }
                    ShellMenuItem { command: "GLOBAL_NEXT_AI_CHANGE"; iconRole: "chevron-down"; objectName: "nextAiChangeMenuItem"; action: nextAiChangeAction }
                    ShellMenuItem { command: "GLOBAL_PREVIOUS_AI_CHANGE"; iconRole: "chevron-left"; objectName: "previousAiChangeMenuItem"; action: previousAiChangeAction }
                    MenuSeparator {}
                    ShellMenuItem { command: "GLOBAL_KEEP_ALL_AI_CHANGES"; iconRole: "review-keep"; objectName: "keepAllAiChangesMenuItem"; action: keepAllAiChangesAction }
                    ShellMenuItem { command: "GLOBAL_UNDO_ALL_AI_CHANGES"; iconRole: "undo"; objectName: "undoAllAiChangesMenuItem"; action: undoAllAiChangesAction }
                    MenuSeparator {}
                    ShellMenuItem { command: "GLOBAL_SHOW_AI_REVIEW"; iconRole: "show"; objectName: "showAiReviewMenuItem"; action: showAiReviewAction }
                }
                onObjectAdded: (index, object) => editMenu.insertMenu(root.menuIndexOf(editMenu, historyMenuItem) + 1, object)
                onObjectRemoved: (index, object) => editMenu.removeMenu(object)
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_SELECT_LINES"
                help: qsTr("Selects lines by expressions")
                iconRole: "select-lines"
                objectName: "selectLinesMenuItem"
                action: Action {
                    id: selectLinesAction
                    text: qsTr("Select &lines")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SELECT_LINES")) root.lazy(selectLinesDialogLoader).openDialog()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SEARCH"
                help: qsTr("Opens the Find panel")
                iconRole: "search"
                objectName: "findMenuItem"
                action: Action {
                    id: findAction
                    text: qsTr("&Find…")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SEARCH")) root.openFind(false)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_REPLACE"
                help: qsTr("Opens the Find panel with the replace row shown")
                iconRole: "find-replace"
                objectName: "findReplaceMenuItem"
                action: Action {
                    id: findReplaceAction
                    text: qsTr("Re&place…")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_REPLACE")) root.openFind(true)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_NEXT"
                help: qsTr("Selects the next match")
                iconRole: "match-next"
                objectName: "findNextMenuItem"
                action: Action {
                    id: findNextAction
                    text: qsTr("Find next")
                    enabled: root.editor.hasLine && !root.app.findBusy
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_NEXT")) root.app.findNext()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_PREVIOUS"
                help: qsTr("Selects the previous match")
                iconRole: "match-previous"
                objectName: "findPreviousMenuItem"
                action: Action {
                    id: findPreviousAction
                    text: qsTr("Find previous")
                    enabled: root.editor.hasLine && !root.app.findBusy
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_PREVIOUS")) root.app.findPrevious()
                }
            }
            MenuSeparator {}
            ShellMenuItem {
                command: "GLOBAL_INSERT_HARD_SPACE"
                help: qsTr("Inserts a hard space (%1) at the cursor in the line editor").arg("\\h")
                iconRole: "hard-space"
                objectName: "insertHardSpaceMenuItem"
                action: Action {
                    id: insertHardSpaceAction
                    text: qsTr("Insert hard space")
                    enabled: root.editor.editable
                    onTriggered: {
                        if (root.hotkeyGesture("GLOBAL_INSERT_HARD_SPACE"))
                            return
                        const field = root.editedField()
                        root.editor.insertHardSpace(field.role, field.selectionStart, field.selectionEnd)
                        root.focusPanel(field, Qt.ShortcutFocusReason)
                    }
                }
            }
        }
        ShellMenu {
            objectName: "subtitlesMenu"
            title: qsTr("&Subtitles")
            ShellMenuItem {
                command: "GLOBAL_SHOW_SHIFT_TIMES"
                help: qsTr("Shifting subtitle times")
                id: showShiftTimesItem
                iconRole: "shift-times"
                objectName: "showShiftTimes"
                text: qsTr("Shift &times…")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_SHIFT_TIMES")) root.showPanel(timingDock)
            }
            ShellMenuItem {
                command: "GLOBAL_SHOW_TIMING_POSTPROCESSOR"
                help: qsTr("Moves line times by the lead-in, lead-out, gap and keyframe rules")
                id: showPostprocessorItem
                iconRole: "timing-postprocessor"
                objectName: "showTimingPostprocessor"
                text: qsTr("Timing &postprocessor…")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_TIMING_POSTPROCESSOR")) root.showPanel(postprocessorDock)
            }
            ShellMenu {
                objectName: "timingHelpersMenu"
                title: qsTr("Timing helpers")
                iconRole: "timing-assistant"
                ShellMenuItem {
                    command: "GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME"
                    help: qsTr("Moves the start of each selected line to the keyframe before it")
                    id: snapStartToPreviousKeyframeItem
                    iconRole: "snap-start-to-previous-keyframe"
                    objectName: "snapStartToPreviousKeyframeItem"
                    text: qsTr("Start to previous keyframe")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME")) root.runTimingHelper("GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME")
                }
                ShellMenuItem {
                    command: "GLOBAL_SNAP_START_TO_NEXT_KEYFRAME"
                    help: qsTr("Moves the start of each selected line to the keyframe after it, staying before its end")
                    id: snapStartToNextKeyframeItem
                    iconRole: "snap-start-to-next-keyframe"
                    objectName: "snapStartToNextKeyframeItem"
                    text: qsTr("Start to next keyframe")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SNAP_START_TO_NEXT_KEYFRAME")) root.runTimingHelper("GLOBAL_SNAP_START_TO_NEXT_KEYFRAME")
                }
                ShellMenuItem {
                    command: "GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME"
                    help: qsTr("Moves the end of each selected line to the keyframe before it, staying after its start")
                    id: snapEndToPreviousKeyframeItem
                    iconRole: "snap-end-to-previous-keyframe"
                    objectName: "snapEndToPreviousKeyframeItem"
                    text: qsTr("End to previous keyframe")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME")) root.runTimingHelper("GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME")
                }
                ShellMenuItem {
                    command: "GLOBAL_SNAP_END_TO_NEXT_KEYFRAME"
                    help: qsTr("Moves the end of each selected line to the keyframe after it")
                    id: snapEndToNextKeyframeItem
                    iconRole: "snap-end-to-next-keyframe"
                    objectName: "snapEndToNextKeyframeItem"
                    text: qsTr("End to next keyframe")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SNAP_END_TO_NEXT_KEYFRAME")) root.runTimingHelper("GLOBAL_SNAP_END_TO_NEXT_KEYFRAME")
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_ADD_LEAD_IN"
                    help: qsTr("Starts each selected line earlier by the lead-in, no earlier than the line before it ends")
                    id: addLeadInItem
                    iconRole: "lead-in"
                    objectName: "addLeadInItem"
                    text: qsTr("Add lead-in")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ADD_LEAD_IN")) root.runTimingHelper("GLOBAL_ADD_LEAD_IN")
                }
                ShellMenuItem {
                    command: "GLOBAL_REMOVE_LEAD_IN"
                    help: qsTr("Starts each selected line later by the lead-in, keeping at least one frame")
                    id: removeLeadInItem
                    iconRole: "lead-in-remove"
                    objectName: "removeLeadInItem"
                    text: qsTr("Remove lead-in")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_REMOVE_LEAD_IN")) root.runTimingHelper("GLOBAL_REMOVE_LEAD_IN")
                }
                ShellMenuItem {
                    command: "GLOBAL_ADD_LEAD_OUT"
                    help: qsTr("Ends each selected line later by the lead-out, no later than the next line starts")
                    id: addLeadOutItem
                    iconRole: "lead-out"
                    objectName: "addLeadOutItem"
                    text: qsTr("Add lead-out")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ADD_LEAD_OUT")) root.runTimingHelper("GLOBAL_ADD_LEAD_OUT")
                }
                ShellMenuItem {
                    command: "GLOBAL_REMOVE_LEAD_OUT"
                    help: qsTr("Ends each selected line earlier by the lead-out, keeping at least one frame")
                    id: removeLeadOutItem
                    iconRole: "lead-out-remove"
                    objectName: "removeLeadOutItem"
                    text: qsTr("Remove lead-out")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_REMOVE_LEAD_OUT")) root.runTimingHelper("GLOBAL_REMOVE_LEAD_OUT")
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_START_FRAME_EARLIER"
                    help: qsTr("Moves the start of each selected line one frame earlier")
                    id: startFrameEarlierItem
                    iconRole: "time-offset-start"
                    objectName: "startFrameEarlierItem"
                    text: qsTr("Start one frame earlier")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_START_FRAME_EARLIER")) root.runTimingHelper("GLOBAL_START_FRAME_EARLIER")
                }
                ShellMenuItem {
                    command: "GLOBAL_START_FRAME_LATER"
                    help: qsTr("Moves the start of each selected line one frame later")
                    id: startFrameLaterItem
                    iconRole: "time-offset-start"
                    objectName: "startFrameLaterItem"
                    text: qsTr("Start one frame later")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_START_FRAME_LATER")) root.runTimingHelper("GLOBAL_START_FRAME_LATER")
                }
                ShellMenuItem {
                    command: "GLOBAL_END_FRAME_EARLIER"
                    help: qsTr("Moves the end of each selected line one frame earlier")
                    id: endFrameEarlierItem
                    iconRole: "time-offset-end"
                    objectName: "endFrameEarlierItem"
                    text: qsTr("End one frame earlier")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_END_FRAME_EARLIER")) root.runTimingHelper("GLOBAL_END_FRAME_EARLIER")
                }
                ShellMenuItem {
                    command: "GLOBAL_END_FRAME_LATER"
                    help: qsTr("Moves the end of each selected line one frame later")
                    id: endFrameLaterItem
                    iconRole: "time-offset-end"
                    objectName: "endFrameLaterItem"
                    text: qsTr("End one frame later")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_END_FRAME_LATER")) root.runTimingHelper("GLOBAL_END_FRAME_LATER")
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_EXTEND_PREVIOUS_TO_START"
                    help: qsTr("Ends the line before the active line where the active line starts")
                    id: extendPreviousToStartItem
                    iconRole: "extend-previous-to-start"
                    objectName: "extendPreviousToStartItem"
                    text: qsTr("Extend previous line to this start")
                    enabled: root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_EXTEND_PREVIOUS_TO_START")) root.runTimingHelper("GLOBAL_EXTEND_PREVIOUS_TO_START")
                }
                ShellMenuItem {
                    command: "GLOBAL_EXTEND_NEXT_TO_END"
                    help: qsTr("Starts the line after the active line where the active line ends")
                    id: extendNextToEndItem
                    iconRole: "extend-next-to-end"
                    objectName: "extendNextToEndItem"
                    text: qsTr("Extend next line to this end")
                    enabled: root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_EXTEND_NEXT_TO_END")) root.runTimingHelper("GLOBAL_EXTEND_NEXT_TO_END")
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_TIMING_ASSISTANT"
                    help: qsTr("Steps through the lines one at a time with a suggested start and end")
                    id: timingAssistantItem
                    iconRole: "timing-assistant"
                    objectName: "timingAssistantItem"
                    text: qsTr("Timing assistant")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_TIMING_ASSISTANT")) root.runTimingHelper("GLOBAL_TIMING_ASSISTANT")
                }
                ShellMenuItem {
                    command: "GLOBAL_TIMING_ASSISTANT_ACCEPT"
                    help: qsTr("Applies the timing assistant’s suggestion to the active line and goes to the next one")
                    id: acceptTimingSuggestionItem
                    iconRole: "commit"
                    objectName: "acceptTimingSuggestionItem"
                    text: qsTr("Accept assistant suggestion")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_TIMING_ASSISTANT_ACCEPT")) root.runTimingHelper("GLOBAL_TIMING_ASSISTANT_ACCEPT")
                }
            }
            ShellMenuItem {
                command: "GLOBAL_BLUR_AND_GLOW_LAYERS"
                help: qsTr("Builds glow, border and shadow layers under the selected lines")
                iconRole: "layers"
                objectName: "blurAndGlowLayersMenuItem"
                action: Action {
                    id: blurAndGlowLayersAction
                    text: qsTr("&Blur and glow layers…")
                    enabled: root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_BLUR_AND_GLOW_LAYERS")) root.openBlurAndGlowLayers()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT"
                help: qsTr("Builds the layers again with the values Blur and glow layers last built with")
                iconRole: "layers-repeat"
                objectName: "blurAndGlowLayersRepeatMenuItem"
                text: qsTr("Build layers with the &last values")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT")) root.repeatBlurAndGlowLayers()
            }
            ShellMenuItem {
                command: "GLOBAL_SHOW_QC_NOTES"
                help: qsTr("Notes from a QC pass, kept in the file, with the lines they mark")
                id: showQcNotesItem
                iconRole: "qc-notes"
                objectName: "showQcNotes"
                text: qsTr("&QC notes…")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_QC_NOTES")) root.showPanel(qcNotesDock)
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_STYLE_MANAGER"
                help: qsTr("Is used to manage ASS styles")
                id: styleManagerItem
                iconRole: "styles"
                objectName: "styleManagerMenuItem"
                text: qsTr("Style &manager")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_STYLE_MANAGER")) root.lazy(styleManagerWindowLoader).showFor(root.app.activeLineStyle())
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_ASS_PROPERTIES"
                help: qsTr("Opens Script properties")
                id: assPropertiesItem
                iconRole: "script-properties"
                objectName: "assProperties"
                text: qsTr("Script properties")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_ASS_PROPERTIES")) root.openScriptProperties()
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_FONT_COLLECTOR"
                help: qsTr("Font collector")
                id: fontCollectorItem
                objectName: "fontCollectorMenuItem"
                iconRole: "font-collector"
                text: qsTr("Font collector")
                enabled: root.shell.hasEditingTarget && root.shell.assColumns
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_FONT_COLLECTOR")) root.openFontCollector()
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_SUBS_RESAMPLE"
                help: qsTr("Resample subtitles")
                id: resampleItem
                iconRole: "resample"
                objectName: "resampleMenuItem"
                text: qsTr("Resample subtitles")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS_RESAMPLE")) root.openResample()
            }
            ShellMenu {
                help: qsTr("Changes the frame rate of the subtitle times")
                iconRole: "fps"
                objectName: "setFpsMenu"
                title: qsTr("Set FPS")
                enabled: root.shell.hasEditingTarget
                ShellMenuItem {
                    command: "GRID_SET_NEW_FPS"
                    iconRole: "fps"
                    objectName: "setNewFps"
                    text: qsTr("Set new FPS…")
                    onTriggered: if (!root.gridGesture(this, "GRID_SET_NEW_FPS")) root.openSetNewFps()
                }
                ShellMenuItem {
                    command: "GRID_SET_FPS_FROM_VIDEO"
                    iconRole: "fps-from-video"
                    objectName: "setFpsFromVideo"
                    text: qsTr("Set FPS from video")
                    enabled: root.video.hasVideo
                    onTriggered: if (!root.gridGesture(this, "GRID_SET_FPS_FROM_VIDEO")) root.app.setFpsFromVideo()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_SPELLCHECKER"
                help: qsTr("Check spelling")
                id: checkSpellingItem
                iconRole: "spellchecker"
                objectName: "checkSpellingMenuItem"
                text: qsTr("Check spelling")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SPELLCHECKER")) root.openSpellChecker()
            }
            ShellMenuItem {
                command: "GLOBAL_CHECK_KARAOKE"
                help: qsTr("Checks the karaoke syllables of every line")
                id: checkKaraokeItem
                iconRole: "karaoke-check"
                objectName: "checkKaraokeMenuItem"
                text: qsTr("Check karaoke")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_CHECK_KARAOKE")) root.openKaraokeCheck()
            }
            ShellMenuItem {
                command: "GLOBAL_KANJI_TIMER"
                help: qsTr("Copies the karaoke timing of romaji lines to their kanji lines")
                id: kanjiTimerItem
                iconRole: "kanji-timer"
                objectName: "kanjiTimerMenuItem"
                text: qsTr("Kanji timer")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_KANJI_TIMER")) root.openKanjiTimer()
            }
            MenuSeparator {}
            ShellMenuItem {
                command: "GLOBAL_TRANSLATION_MODE"
                help: root.app.translationHint.length > 0 ? root.app.translationHint
                    : qsTranslate("HikariSub.Translation", "Translator mode displays and saves both foreign text and translation text")
                iconRole: "translation-mode"
                objectName: "translationModeMenuItem"
                action: Action {
                    id: translationModeAction
                    text: qsTranslate("HikariSub.Translation", "Translation mode")
                    checkable: true
                    checked: root.editor.translationMode
                    enabled: root.shell.hasEditingTarget && (root.editor.translationMode || root.app.translatorModeAvailable)
                    onToggled: {
                        if (!root.hotkeyGesture("GLOBAL_TRANSLATION_MODE")) {
                            if (checked)
                                root.app.turnOnTranslationMode()
                            else
                                root.openInMainWindow(translationModeOffConfirm)
                        }
                        checked = Qt.binding(() => root.editor.translationMode)
                    }
                }
            }
        }
        ShellMenu {
            objectName: "videoMenu"
            title: qsTr("&Video")
            ShellMenuItem {
                command: "GLOBAL_OPEN_VIDEO"
                help: qsTr("Opens video file")
                iconRole: "open-video"
                action: Action {
                    text: qsTr("Open &video…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.show()
                }
            }
            RecentFilesMenu {
                iconRole: "recent-video"
                objectName: "recentVideoMenu"
                prefix: "recentVideo"
                title: qsTr("Recently opened videos")
                load: () => root.app.recentVideos()
                onChosen: path => root.video.openVideo(path)
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_KEYFRAMES"
                objectName: "openKeyframesMenuItem"
                help: qsTr("Open keyframes")
                iconRole: "open-keyframes"
                action: Action {
                    id: openKeyframesAction
                    text: qsTr("Open keyframes")
                    enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_KEYFRAMES")) keyframesDialog.show()
                }
            }
            RecentFilesMenu {
                iconRole: "recent-keyframes"
                objectName: "recentKeyframesMenu"
                prefix: "recentKeyframes"
                title: qsTr("Recently opened keyframes")
                enabled: root.video.hasVideo
                load: () => root.app.recentKeyframes()
                onChosen: path => {
                    const problem = root.app.openKeyframesFile(path)
                    if (problem.length > 0)
                        root.notices.post("error", qsTr("Open keyframes"), problem)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_TIMECODES"
                objectName: "openTimecodesMenuItem"
                help: qsTr("Open a VFR timecodes v1, v2 or v4 file")
                iconRole: "open-timecodes"
                action: Action {
                    id: openTimecodesAction
                    text: qsTr("Open timecodes")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_TIMECODES")) timecodesDialog.show()
                }
            }
            RecentFilesMenu {
                iconRole: "recent-timecodes"
                objectName: "recentTimecodesMenu"
                prefix: "recentTimecodes"
                title: qsTr("Recently opened timecodes")
                load: () => root.app.recentTimecodes()
                onChosen: path => root.openTimecodes(path)
            }
            ShellMenuItem {
                command: "GLOBAL_SAVE_TIMECODES"
                objectName: "saveTimecodesMenuItem"
                help: qsTr("Save a VFR timecodes v2 file")
                iconRole: "save-timecodes"
                action: Action {
                    id: saveTimecodesAction
                    text: qsTr("Save timecodes")
                    enabled: root.video.hasVideo || root.app.timecodesFile.length > 0
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_TIMECODES")) saveTimecodesDialog.show()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_CLOSE_TIMECODES"
                objectName: "closeTimecodesMenuItem"
                help: qsTr("Close the currently open timecodes file")
                iconRole: "close-timecodes"
                action: Action {
                    id: closeTimecodesAction
                    text: qsTr("Close timecodes")
                    enabled: root.app.timecodesFile.length > 0
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_TIMECODES")) root.app.closeTimecodes()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_DUMMY_VIDEO"
                iconRole: "dummy-video"
                objectName: "dummyVideoMenuItem"
                action: Action {
                    id: dummyVideoAction
                    text: qsTr("Open dummy video")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_DUMMY_VIDEO")) root.openDummyVideo()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_CENTER_TIMES_IN_FRAMES"
                help: qsTr("Moves the selected lines’ start and end times to the middle of their frames, so a later shift by time keeps them on whole frames")
                iconRole: "center-times"
                objectName: "centerTimesInFramesMenuItem"
                action: Action {
                    id: centerTimesInFramesAction
                    text: qsTr("Centre times within frames")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CENTER_TIMES_IN_FRAMES")) root.app.centerTimesInFrames()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_ALIGN_TO_VIDEO"
                help: qsTr("Times the active line to the frames around the video’s frame where a picked point keeps its colour")
                iconRole: "align-to-video"
                objectName: "alignToVideoMenuItem"
                action: Action {
                    id: alignToVideoAction
                    text: qsTr("Align to video…")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ALIGN_TO_VIDEO")) root.openAlignToVideo()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_TRACK_COLOUR"
                help: qsTr("Follows a picked point’s colour across the selected lines’ frames and writes it as colour or alpha tags")
                iconRole: "track-colour"
                objectName: "trackColourMenuItem"
                action: Action {
                    id: trackColourAction
                    text: qsTr("Track colour…")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_TRACK_COLOUR")) root.openVideoAnalysis("track")
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_FADE"
                help: qsTr("Finds where the active line’s sign fades in and out at a picked point and writes %1").arg("\\fad")
                iconRole: "find-fade"
                objectName: "findFadeMenuItem"
                action: Action {
                    id: findFadeAction
                    text: qsTr("Find fade…")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_FADE")) root.openVideoAnalysis("fade")
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_START_TIME"
                iconRole: "set-start-time"
                objectName: "setStartTimeMenuItem"
                action: Action {
                    id: setStartTimeAction
                    text: qsTr("Insert start time from video")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_START_TIME")) root.app.setTimeFromVideo(false)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_END_TIME"
                iconRole: "set-end-time"
                objectName: "setEndTimeMenuItem"
                action: Action {
                    id: setEndTimeAction
                    text: qsTr("Insert end time from video")
                    enabled: root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_END_TIME")) root.app.setTimeFromVideo(true)
                }
            }
            ShellMenu {
                iconRole: "shift-times"
                objectName: "shiftFramesToVideoMenu"
                title: qsTr("Shift by frames to video")
                enabled: root.video.hasVideo && root.shell.hasEditingTarget
                ShellMenuItem {
                    command: "GLOBAL_SHIFT_FRAMES_TO_VIDEO_START"
                    help: qsTr("Shifts the selected lines by whole frames so the active line starts on the video frame")
                    iconRole: "shift-start-to-video"
                    objectName: "shiftFramesToVideoStartMenuItem"
                    action: Action {
                        id: shiftFramesToVideoStartAction
                        text: qsTr("Selected lines, start at video")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_FRAMES_TO_VIDEO_START")) root.app.shiftFramesToVideo(false, false)
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SHIFT_FRAMES_TO_VIDEO_END"
                    help: qsTr("Shifts the selected lines by whole frames so the active line ends on the video frame")
                    iconRole: "shift-end-to-video"
                    objectName: "shiftFramesToVideoEndMenuItem"
                    action: Action {
                        id: shiftFramesToVideoEndAction
                        text: qsTr("Selected lines, end at video")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_FRAMES_TO_VIDEO_END")) root.app.shiftFramesToVideo(true, false)
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_START"
                    help: qsTr("Shifts the active line and every line after it by whole frames so the active line starts on the video frame")
                    iconRole: "shift-start-to-video"
                    objectName: "shiftFollowingFramesToVideoStartMenuItem"
                    action: Action {
                        id: shiftFollowingFramesToVideoStartAction
                        text: qsTr("Following lines, start at video")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_START")) root.app.shiftFramesToVideo(false, true)
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_END"
                    help: qsTr("Shifts the active line and every line after it by whole frames so the active line ends on the video frame")
                    iconRole: "shift-end-to-video"
                    objectName: "shiftFollowingFramesToVideoEndMenuItem"
                    action: Action {
                        id: shiftFollowingFramesToVideoEndAction
                        text: qsTr("Following lines, end at video")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_END")) root.app.shiftFramesToVideo(true, true)
                    }
                }
            }
            ShellMenu {
                iconRole: "motion-apply"
                objectName: "trackingMenu"
                title: qsTr("Tracking")
                enabled: root.shell.hasEditingTarget
                ShellMenuItem {
                    command: "GLOBAL_APPLY_MOTION_DATA"
                    help: root.video.hasVideo ? qsTr("Applies After Effects motion data from Mocha or Blender to the selected lines, one line a frame")
                                              : qsTr("Open a video first: tracking data is per frame.")
                    iconRole: "motion-apply"
                    objectName: "applyMotionDataMenuItem"
                    action: Action {
                        id: applyMotionDataAction
                        text: qsTr("Apply motion data…")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_APPLY_MOTION_DATA")) root.openMotionData()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_PERSPECTIVE_TRACKING"
                    help: root.video.hasVideo ? qsTr("Applies Power-Pin tracking data to the selected lines, one line a frame")
                                              : qsTr("Open a video first: tracking data is per frame.")
                    iconRole: "tool-perspective"
                    objectName: "perspectiveTrackingMenuItem"
                    action: Action {
                        id: perspectiveTrackingAction
                        text: qsTr("Perspective tracking…")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_PERSPECTIVE_TRACKING")) root.openPerspectiveTracking()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_REVERT_MOTION_DATA"
                    help: qsTr("Puts the tracked lines of the selection back as the lines they were made from")
                    iconRole: "motion-revert"
                    objectName: "revertMotionDataMenuItem"
                    action: Action {
                        id: revertMotionDataAction
                        text: qsTr("Revert motion data")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_REVERT_MOTION_DATA")) root.revertMotionData()
                    }
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_COPY_MOTION_DATA"
                    help: root.video.hasVideo ? qsTr("Copies the selected frame-by-frame lines’ position, scale and rotation as motion data")
                                              : qsTr("Open a video first: tracking data is per frame.")
                    iconRole: "motion-copy"
                    objectName: "copyMotionDataMenuItem"
                    action: Action {
                        id: copyMotionDataAction
                        text: qsTr("Copy motion data from lines")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_COPY_MOTION_DATA")) root.trackingToast(root.app.copyMotionData())
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_COPY_POWER_PIN_TRACK"
                    help: root.video.hasVideo ? qsTr("Copies the selected lines’ perspective planes as Power-Pin data, one quad a frame")
                                              : qsTr("Open a video first: tracking data is per frame.")
                    iconRole: "perspective-plane"
                    objectName: "copyPowerPinTrackMenuItem"
                    action: Action {
                        id: copyPowerPinTrackAction
                        text: qsTr("Copy Power-Pin track from lines")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_COPY_POWER_PIN_TRACK")) root.trackingToast(root.app.copyPowerPinTrack())
                    }
                }
                MenuSeparator {}
                ShellMenuItem {
                    command: "GLOBAL_EXTEND_TRACKED_LINES"
                    help: root.video.hasVideo ? qsTr("Fits the selected frame-by-frame lines’ motion and carries it on for more frames")
                                              : qsTr("Open a video first: tracking data is per frame.")
                    iconRole: "motion-extend"
                    objectName: "extendTrackedLinesMenuItem"
                    action: Action {
                        id: extendTrackedLinesAction
                        text: qsTr("Extend tracked lines…")
                        enabled: root.video.hasVideo && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_EXTEND_TRACKED_LINES")) root.openExtendTracking()
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_EXTRAPOLATE_MOVE"
                    help: qsTr("Carries each selected line’s timed %1 on at the same speed from its start to its end").arg("\\move")
                    iconRole: "move-extend"
                    objectName: "extrapolateMoveMenuItem"
                    action: Action {
                        id: extrapolateMoveAction
                        text: qsTr("Extrapolate %1 to the line").arg("\\move")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_EXTRAPOLATE_MOVE")) root.trackingToast(root.app.extrapolateMoves())
                    }
                }
            }
            ShellMenuItem {
                command: "GLOBAL_PREVIOUS_FRAME"
                help: qsTr("Go to previous frame")
                iconRole: "frame-previous"
                action: Action {
                    id: previousFrameAction
                    text: qsTr("Previous frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PREVIOUS_FRAME")) root.video.stepFrames(-1)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_NEXT_FRAME"
                help: qsTr("Go to next frame")
                iconRole: "frame-next"
                action: Action {
                    id: nextFrameAction
                    text: qsTr("Next frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_NEXT_FRAME")) root.video.stepFrames(1)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_VIDEO_AT_START_TIME"
                help: qsTr("Moves video to start time")
                iconRole: "video-to-start-time"
                action: Action {
                    id: goToStartAction
                    text: qsTr("Go to start time of line"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_START_TIME")) root.video.goToLineStart()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_VIDEO_AT_END_TIME"
                help: qsTr("Moves video to end time")
                iconRole: "video-to-end-time"
                action: Action {
                    id: goToEndAction
                    text: qsTr("Go to end time of line"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_END_TIME")) root.video.goToLineEnd()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_PLAY_PAUSE"
                help: qsTr("Plays or pauses the video")
                iconRole: root.video.playing ? "media-pause" : "media-play"
                action: Action {
                    id: playPauseAction
                    text: qsTr("Play or pause"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PLAY_PAUSE")) root.video.togglePlay()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_GO_TO_PREVIOUS_KEYFRAME"
                iconRole: "keyframe-previous"
                action: Action {
                    id: previousKeyframeAction
                    text: qsTr("Go to previous keyframe"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_PREVIOUS_KEYFRAME")) root.video.previousKeyframe()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_GO_TO_NEXT_KEYFRAME"
                iconRole: "keyframe-next"
                action: Action {
                    id: nextKeyframeAction
                    text: qsTr("Go to next keyframe"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_NEXT_KEYFRAME")) root.video.nextKeyframe()
                }
            }
            MenuSeparator {}
            ShellMenuItem {
                iconRole: "close-video"
                objectName: "unloadVideoMenuItem"
                text: qsTr("Unload video")
                enabled: root.video.loaded
                onTriggered: if (!root.hotkeyGesture("VIDEO_DELETE_FILE", 3)) root.video.unloadVideo()
            }
            ShellMenuItem {
                command: "GLOBAL_RELOAD_VIDEO"
                iconRole: "reload-video"
                objectName: "reloadVideoMenuItem"
                action: Action {
                    id: reloadVideoAction
                    text: qsTr("Reload video")
                    enabled: root.video.hasVideo && !root.video.dummy
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_RELOAD_VIDEO")) root.video.reloadVideo()
                }
            }
            VideoStreamsMenu {
                objectName: "videoStreamsMenu"
                video: root.video
            }
            VideoChaptersMenu {
                objectName: "videoChaptersMenu"
                video: root.video
            }
            ShellMenuItem {
                command: "GLOBAL_VIDEO_DETAILS"
                iconRole: "video-details"
                objectName: "videoDetailsMenuItem"
                action: Action {
                    id: videoDetailsAction
                    text: qsTr("Video details…"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_VIDEO_DETAILS")) root.openVideoDetails()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_VIDEO_ZOOM"
                iconRole: "zoom"
                objectName: "videoZoomMenuItem"
                action: Action {
                    id: videoZoomAction
                    text: qsTr("Zoom video"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_VIDEO_ZOOM")) root.videoView.toggleZoom()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_RESET_VIDEO_ZOOM"
                iconRole: "zoom-reset"
                objectName: "resetVideoZoomMenuItem"
                action: Action {
                    id: resetVideoZoomAction
                    text: qsTr("Reset zoom and pan"); enabled: root.videoView.zoomed
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_RESET_VIDEO_ZOOM")) root.videoView.resetZoom()
                }
            }
            ShellMenuItem {
                iconRole: "render-cost"
                command: "GLOBAL_VIDEO_RENDER_COST"
                objectName: "renderCostMenuItem"
                action: Action {
                    id: renderCostAction
                    text: qsTr("Render cost"); enabled: root.video.hasVideo
                    checkable: true
                    checked: root.videoView.renderCost
                    onTriggered: {
                        checked = Qt.binding(() => root.videoView.renderCost)
                        if (!root.hotkeyGesture("GLOBAL_VIDEO_RENDER_COST")) root.videoView.toggleRenderCost()
                    }
                }
            }
        }
        ShellMenu {
            id: audioMenu
            objectName: "audioMenu"
            title: qsTr("A&udio")
            ShellMenuItem {
                command: "GLOBAL_OPEN_AUDIO"
                help: qsTr("Opens audio file")
                iconRole: "open-audio"
                objectName: "openAudioMenuItem"
                action: Action {
                    id: openAudioAction
                    text: qsTr("Open audio")
                    onTriggered: {
                        if (root.hotkeyGesture("GLOBAL_OPEN_AUDIO"))
                            return
                        audioDialog.currentFolder = root.startFolder("audio", root.app.audioDialogFolder())
                        audioDialog.open()
                    }
                }
            }
            ShellMenu {
                help: qsTr("Recently opened audio")
                id: recentAudioMenu
                iconRole: "recent-audio"
                objectName: "recentAudioMenu"
                title: qsTr("Recently opened audio")
                property var rows: []
                onAboutToShow: rows = root.app.recentAudio()
                Instantiator {
                    model: recentAudioMenu.rows
                    delegate: ShellMenuItem {
                        required property var modelData
                        required property int index
                        objectName: "recentAudio" + index
                        help: qsTr("Open") + " " + modelData.path
                        text: modelData.label
                        onTriggered: if (!root.app.revealRecent(modelData.path)) root.audio.openAudio(modelData.path)
                    }
                    onObjectAdded: (index, object) => recentAudioMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentAudioMenu.removeItem(object)
                }
                ShellMenuItem {
                    text: qsTr("None")
                    enabled: false
                    visible: recentAudioMenu.rows.length === 0
                    height: visible ? implicitHeight : 0
                }
            }
            ShellMenuItem {
                command: "GLOBAL_AUDIO_FROM_VIDEO"
                help: qsTr("Opens audio from video")
                iconRole: "audio-from-video"
                objectName: "audioFromVideoMenuItem"
                action: Action {
                    id: audioFromVideoAction
                    text: qsTr("Open audio from video")
                    enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUDIO_FROM_VIDEO")) root.app.openAudioFromVideo()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_CLOSE_AUDIO"
                help: qsTr("Closes audio")
                iconRole: "close-audio"
                objectName: "closeAudioMenuItem"
                action: Action {
                    id: closeAudioAction
                    text: qsTr("Close audio")
                    enabled: root.audio.hasAudio
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_AUDIO")) root.audio.closeAudio()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_RELOAD_AUDIO"
                iconRole: "reload-audio"
                objectName: "reloadAudioMenuItem"
                action: Action {
                    id: reloadAudioAction
                    text: qsTr("Reload audio")
                    enabled: root.audio.reloadable
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_RELOAD_AUDIO")) root.audio.reloadAudio()
                }
            }
            MenuSeparator {}
            ShellMenuItem {
                command: "GLOBAL_SET_AUDIO_FROM_VIDEO"
                iconRole: "audio-to-video-time"
                objectName: "setAudioFromVideoMenuItem"
                action: Action {
                    id: setAudioFromVideoAction
                    text: qsTr("Set audio position to video time")
                    enabled: root.audio.hasAudio && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_AUDIO_FROM_VIDEO")) root.app.setAudioFromVideo(false)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_AUDIO_MARK_FROM_VIDEO"
                iconRole: "audio-marker-to-video-time"
                objectName: "setAudioMarkFromVideoMenuItem"
                action: Action {
                    id: setAudioMarkFromVideoAction
                    text: qsTr("Set audio marker to video time")
                    enabled: root.audio.hasAudio && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_AUDIO_MARK_FROM_VIDEO")) root.app.setAudioFromVideo(true)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_JOIN_PREVIOUS_AUDIO_SELECTION"
                help: qsTr("Gives the active line the audio selection and ends the line before it where the selection starts")
                iconRole: "join-lines"
                objectName: "joinPreviousAudioSelectionMenuItem"
                action: Action {
                    id: joinPreviousAudioSelectionAction
                    text: qsTr("Join with previous line using the audio selection")
                    enabled: root.audio.ready && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_JOIN_PREVIOUS_AUDIO_SELECTION")) root.app.joinPreviousToAudioSelection()
                }
            }
        }
        ShellMenu {
            id: automationMenu
            objectName: "automationMenu"
            title: qsTr("&Automation")
            onAboutToShow: root.automation.menuOpened()
            ShellMenuItem {
                command: "GLOBAL_AUTOMATION_LOAD_SCRIPT"
                help: qsTr("Load script")
                iconRole: "automation"
                objectName: "loadScriptMenuItem"
                action: Action {
                    id: loadScriptAction
                    text: qsTr("&Load script…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_SCRIPT")) scriptDialog.open()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_AUTOMATION_RELOAD_AUTOLOAD"
                help: qsTr("Refresh autoload scripts")
                iconRole: "automation"
                objectName: "reloadAutoloadMenuItem"
                action: Action {
                    id: reloadAutoloadAction
                    text: qsTr("Refresh autoload scripts")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_RELOAD_AUTOLOAD")) root.automation.reloadAutoload()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT"
                iconRole: "run-script"
                objectName: "loadLastScriptMenuItem"
                action: Action {
                    id: loadLastScriptAction
                    text: qsTr("Run the last loaded script")
                    enabled: !root.automation.running && !root.workspaceLayout.modalOpen
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT")) root.automation.runLastLoadedScript()
                }
            }
            ShellMenuItem {
                iconRole: "rerun-macro"
                objectName: "rerunMenuItem"
                action: Action {
                    text: qsTr("Rerun last macro")
                    enabled: root.automation.canRerun && !root.workspaceLayout.modalOpen
                    onTriggered: root.automation.rerunLast()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_AUTOMATION_REPEAT_LAST_RUN"
                help: qsTr("Run the last macro again with the same answers, without its dialogs when they are unchanged")
                iconRole: "repeat-run"
                objectName: "repeatRunMenuItem"
                action: Action {
                    id: repeatRunAction
                    text: qsTr("Repeat last run")
                    enabled: root.automation.canRepeat && !root.workspaceLayout.modalOpen
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_REPEAT_LAST_RUN")) root.automation.repeatLast()
                }
            }
            ShellMenuItem {
                iconRole: "run-cancelled"
                objectName: "cancelMacroMenuItem"
                visible: root.automation.running
                height: visible ? implicitHeight : 0
                action: Action {
                    text: qsTr("Cancel macro")
                    enabled: root.automation.running && !root.automation.cancelling
                    onTriggered: root.automation.cancelRun()
                }
            }
            ShellMenuItem {
                iconRole: "automation-manager"
                objectName: "automationManagerMenuItem"
                action: Action {
                    text: qsTr("Show &panel")
                    onTriggered: root.showPanel(automationDock)
                }
            }
            ShellMenuItem {
                id: pluginsMenuItem
                command: "GLOBAL_PLUGINS"
                help: qsTr("Install, turn on and off, and uninstall plugins")
                iconRole: "plugins"
                objectName: "pluginsMenuItem"
                action: Action {
                    id: pluginsAction
                    text: qsTr("Pl&ugins…")
                    enabled: !root.workspaceLayout.modalOpen
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PLUGINS")) pluginManagerDialog.show()
                }
            }
            MacroMenuEntries {
                menu: automationMenu
                after: pluginsMenuItem
                macros: root.app.pluginRuntime ? root.app.pluginRuntime.commands : []
                itemPrefix: "pluginCommand_"
                menuPrefix: "pluginCommandMenu_"
                onTriggered: command => root.app.pluginRuntime.run(command.id)
            }
            MenuSeparator {
                id: macrosSeparator
                objectName: "macrosSeparator"
            }
            MacroMenuEntries {
                menu: automationMenu
                after: macrosSeparator
                macros: root.macroItems
                enabled: !root.automation.running
                onTriggered: macro => {
                    if (root.hotkeys.shiftClicked(true)) {
                        root.showPanel(automationDock)
                        automationTool.record(root.automationHotkeys.legacyNameFor(macro.path, macro.ordinal))
                    } else {
                        root.automationManager.run(macro.path, macro.ordinal)
                    }
                }
            }
        }
        ShellMenu {
            id: viewMenu
            objectName: "viewMenu"
            title: qsTr("Vie&w")
            LayoutMenu {
                id: viewLayoutMenu
                objectName: "viewLayoutMenu"
                iconRole: "layout"
                title: qsTr("&Layout")
                prefix: "viewLayout"
                layouts: root.workspaceLayout
                lockAction: lockLayoutAction
                lockReason: root.lockReason
                onPresetChosen: name => root.applyPreset(name)
                onSavedLayoutChosen: name => root.applySavedLayout(name)
                onSaveRequested: root.lazy(saveLayoutDialogLoader).show()
                onResetRequested: root.resetLayout()
                onManageRequested: root.lazy(manageLayoutsDialogLoader).show()
            }
            MenuSeparator {}
            ShellMenu {
                id: panelsMenu
                iconRole: "panels"
                objectName: "panelsMenu"
                title: qsTr("&Panels")
                Instantiator {
                    model: root.builtInDocks
                    delegate: ShellMenu {
                        required property var modelData
                        objectName: "panelMenu" + modelData.uniqueName
                        title: modelData.title
                        ShellMenuItem {
                            iconRole: "show"
                            objectName: "panelShow" + modelData.uniqueName
                            text: qsTr("Show")
                            enabled: root.panelAvailable(modelData) && !root.layoutLocked
                            onTriggered: root.showPanel(modelData)
                        }
                        ShellMenuItem {
                            iconRole: modelData.isFloating ? "panel-dock" : "panel-float"
                            objectName: "panelFloat" + modelData.uniqueName
                            text: modelData.isFloating ? qsTr("Dock") : qsTr("Float")
                            enabled: modelData.isOpen && !root.layoutLocked
                            onTriggered: root.setPanelFloating(modelData, !modelData.isFloating)
                        }
                        MenuSeparator {}
                        ShellMenuItem {
                            iconRole: "tab-close"
                            objectName: "panelClose" + modelData.uniqueName
                            text: qsTr("Close")
                            enabled: modelData.isOpen && !root.layoutLocked
                            onTriggered: root.closePanel(modelData)
                        }
                    }
                    onObjectAdded: (index, object) => panelsMenu.insertMenu(index, object)
                    onObjectRemoved: (index, object) => panelsMenu.removeMenu(object)
                }
                MenuSeparator {
                    visible: root.pluginPanelRows.length > 0
                    height: root.pluginPanelRows.length > 0 ? implicitHeight : 0
                }
                Instantiator {
                    model: root.pluginPanelRows
                    delegate: ShellMenu {
                        id: pluginPanelMenu
                        required property var modelData
                        readonly property var dock: root.pluginDocks.find(d => d.storedName === modelData.dock) ?? null
                        readonly property bool kept: !root.pluginUi || !root.pluginUi.panels.some(r => r.dock === modelData.dock)
                        objectName: "panelMenu" + modelData.dock
                        iconRole: "plugins"
                        title: modelData.title
                        detailText: modelData.pluginName
                        ShellMenuItem {
                            iconRole: "show"
                            objectName: "panelShow" + pluginPanelMenu.modelData.dock
                            text: qsTr("Show")
                            enabled: !pluginPanelMenu.kept && !root.layoutLocked
                            onTriggered: root.showPluginPanel(pluginPanelMenu.modelData.dock)
                        }
                        ShellMenuItem {
                            iconRole: pluginPanelMenu.dock?.isFloating ? "panel-dock" : "panel-float"
                            objectName: "panelFloat" + pluginPanelMenu.modelData.dock
                            text: pluginPanelMenu.dock?.isFloating ? qsTr("Dock") : qsTr("Float")
                            enabled: pluginPanelMenu.dock !== null && pluginPanelMenu.dock.isOpen && !root.layoutLocked
                            onTriggered: root.setPanelFloating(pluginPanelMenu.dock, !pluginPanelMenu.dock.isFloating)
                        }
                        MenuSeparator {}
                        ShellMenuItem {
                            iconRole: "tab-close"
                            objectName: "panelClose" + pluginPanelMenu.modelData.dock
                            text: qsTr("Close")
                            enabled: pluginPanelMenu.dock !== null && pluginPanelMenu.dock.isOpen && !root.layoutLocked
                            onTriggered: root.closePanel(pluginPanelMenu.dock)
                        }
                        ShellMenuItem {
                            iconRole: "remove"
                            objectName: "panelForget" + pluginPanelMenu.modelData.dock
                            text: qsTr("Remove from the layout")
                            visible: pluginPanelMenu.kept
                            height: pluginPanelMenu.kept ? implicitHeight : 0
                            enabled: !root.layoutLocked
                            onTriggered: root.forgetPluginDock(pluginPanelMenu.dock)
                        }
                    }
                    onObjectAdded: (index, object) => panelsMenu.insertMenu(root.builtInDocks.length + 1 + index, object)
                    onObjectRemoved: (index, object) => panelsMenu.removeMenu(object)
                }
            }
            ShellMenuItem {
                iconRole: "panel-move"
                objectName: "movePanel"
                text: qsTr("&Move panel…")
                enabled: !root.layoutLocked
                onTriggered: placementWindow.openFor(root.focusedDock())
            }
            ShellMenuItem {
                iconRole: "layout-restore"
                objectName: "restoreLayoutBackup"
                text: qsTr("Restore the previous layout")
                enabled: root.workspaceLayout.hasBackup && !root.layoutLocked
                onTriggered: root.workspaceLayout.restoreBackup()
            }
            MenuSeparator {}
            ShellMenuItem {
                command: "GLOBAL_NODE_EFFECTS"
                help: qsTranslate("HikariSub.NodeEffects", "Edits this file’s node effects on a canvas, with a preview on the video")
                iconRole: "node-editor"
                objectName: "nodeEffectsMenuItem"
                action: Action {
                    id: nodeEffectsAction
                    text: qsTranslate("HikariSub.NodeEffects", "Node effects")
                    checkable: true
                    checked: root.workspaceLayout.nodeEffects
                    enabled: root.workspaceLayout.nodeEffects || (root.shell.hasEditingTarget && !root.layoutLocked)
                    onToggled: {
                        if (!root.hotkeyGesture("GLOBAL_NODE_EFFECTS"))
                            root.setNodeEffects(checked)
                        checked = Qt.binding(() => root.workspaceLayout.nodeEffects)
                    }
                }
            }
            MenuSeparator {}
            ShellMenuItem {
                iconRole: "log"
                objectName: "logMenuItem"
                action: Action {
                    text: qsTr("Log")
                    enabled: !root.layoutLocked
                    onTriggered: {
                        if (logDock.isOpen)
                            logDock.close()
                        else
                            root.showPanel(logDock)
                    }
                }
            }
            ShellMenuItem {
                command: "GLOBAL_HIDE_TAGS"
                id: hideTagsItem
                iconRole: "hide-tags"
                objectName: "hideTagsMenuItem"
                text: qsTr("Hide tags")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_HIDE_TAGS")) root.app.toggleHideTags()
            }
            ShellMenuItem {
                command: "GLOBAL_EDITOR_HIDE_TAGS"
                iconRole: "hide-tags"
                objectName: "hideEditorTagsMenuItem"
                action: Action {
                    id: hideEditorTagsAction
                    text: qsTr("Hide tags in the line editor")
                    checkable: true
                    checked: !root.editor.showTags
                    onToggled: {
                        if (!root.hotkeyGesture("GLOBAL_EDITOR_HIDE_TAGS"))
                            root.editor.showTags = !checked
                        checked = Qt.binding(() => !root.editor.showTags)
                    }
                }
            }
        }
        ShellMenu {
            title: qsTr("&Help")
            ShellMenuItem {
                command: "GLOBAL_GETTING_STARTED"
                help: qsTr("Walks through the settings most people change first")
                iconRole: "getting-started"
                objectName: "gettingStartedMenuItem"
                action: Action {
                    id: gettingStartedAction
                    text: qsTr("&Getting started")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GETTING_STARTED")) root.lazy(gettingStartedDialogLoader).openSetup()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_HELP"
                help: qsTr("Opens the user guide in the default browser")
                iconRole: "help"
                objectName: "userGuideMenuItem"
                action: Action {
                    id: userGuideAction
                    text: qsTr("User &guide")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HELP")) root.openUserGuide()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_ANSI"
                help: qsTr("Opens a form for a bug, feature request or question on GitHub")
                iconRole: "report-issue"
                objectName: "reportIssueMenuItem"
                action: Action {
                    id: reportIssueAction
                    text: qsTr("&Report an issue")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ANSI")) root.lazy(reportIssueWindowLoader).openForm()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_CHECK_FOR_UPDATES"
                help: qsTr("Checks whether a newer version is available")
                iconRole: "check-updates"
                objectName: "checkForUpdatesMenuItem"
                action: Action {
                    id: checkForUpdatesAction
                    text: qsTr("Check for &updates")
                    enabled: !root.updates.checking
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CHECK_FOR_UPDATES")) root.updates.checkNow()
                }
            }
            ShellMenuItem {
                help: qsTr("Opens the folder that holds your settings, scripts, dictionaries and presets")
                iconRole: "show-in-folder"
                objectName: "openUserFolderMenuItem"
                visible: root.app.userFolder.length > 0
                height: visible ? implicitHeight : 0
                action: Action {
                    id: openUserFolderAction
                    text: qsTr("Open user &folder")
                    onTriggered: root.app.openUserFolder()
                }
            }
            ShellMenuItem {
                help: qsTr("Shows the release notes of the versions since the one you used before")
                iconRole: "whats-new"
                objectName: "whatsNewMenuItem"
                action: Action {
                    id: whatsNewAction
                    text: qsTr("What’s &new")
                    onTriggered: root.lazy(whatsNewDialogLoader).open()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_ABOUT"
                help: qsTr("Shows program info")
                iconRole: "about"
                objectName: "aboutMenuItem"
                action: Action {
                    id: aboutAction
                    text: qsTr("&About")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ABOUT")) root.lazy(aboutDialogLoader).open()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_HELPERS"
                help: qsTr("Shows credits")
                iconRole: "credits"
                objectName: "creditsMenuItem"
                action: Action {
                    id: creditsAction
                    text: qsTr("&Credits")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HELPERS")) root.lazy(creditsDialogLoader).open()
                }
            }
        }
    }

    component RoleField: TextArea {
        id: field
        // MUST KILL: role is a magic int (0 Original, 1 Translated)
        required property int role
        readonly property string shown: role === 0 ? root.editor.text : root.editor.translationText
        property Item tabNext: null
        property Item tabPrevious: null
        readOnly: !root.editor.editable
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: topPadding + bottomPadding + cursorRectangle.height

        property bool syncing: false
        function carriedPrefix(outer, inner) {
            // MUST KILL: 6 * 16384 is a magic cap tied to how much Tags from original carries
            if (outer.length > 6 * 16384 || inner.length > 6 * 16384)
                return -1
            // Block ends by position, not a Set: V4's Set.has is linear.
            const ends = []
            let any = false
            let at = 0
            for (let open = -1; at < outer.length; ++at) {
                const c = outer[at]
                if (open >= 0) {
                    if (c === "{")
                        break
                    if (c === "}") {
                        ends[at + 1] = true
                        any = true
                        open = -1
                    }
                } else if (c === "{") {
                    open = at
                } else if (c === "\\" && "hNn".includes(outer[at + 1] ?? "")) {
                    ++at
                } else if (c !== " " && c !== "\t") {
                    break
                }
            }
            if (!any || inner.length === 0)
                return -1
            const fail = [0]
            for (let i = 1, k = 0; i < inner.length; ++i) {
                while (k > 0 && inner[i] !== inner[k])
                    k = fail[k - 1]
                if (inner[i] === inner[k])
                    ++k
                fail.push(k)
            }
            for (let i = 0, k = 0; i < outer.length && i - k <= at; ++i) {
                while (k > 0 && outer[i] !== inner[k])
                    k = fail[k - 1]
                if (outer[i] === inner[k])
                    ++k
                if (k === inner.length) {
                    if (ends[i + 1 - k] === true)
                        return i + 1 - k
                    k = fail[k - 1]
                }
            }
            return -1
        }
        function sync() {
            if (text === shown)
                return
            syncing = true
            let caret = cursorPosition
            const added = text.length > 0 ? carriedPrefix(shown, text) : -1
            const removed = shown.length > 0 ? carriedPrefix(text, shown) : -1
            if (added > 0)
                caret += added
            else if (removed > 0)
                caret = Math.max(0, caret - removed)
            text = shown
            cursorPosition = Math.min(caret, length)
            syncing = false
        }
        function report() {
            if (syncing)
                return
            if (role === 0)
                root.editor.textEdited(text, cursorPosition)
            else
                root.editor.translationEdited(text, cursorPosition)
        }
        readonly property bool commitsTyping: root.workspaceLayout.preset === "Karaoke"
        Timer {
            id: typingCommit
            interval: 300
            onTriggered: field.commitTyping()
        }
        function commitTyping() {
            typingCommit.stop()
            field.report()
            root.editor.commitTyping()
        }
        onCommitsTypingChanged: if (typingCommit.running) field.commitTyping()
        Component.onCompleted: sync()
        onTextChanged: {
            if (syncing)
                return
            Qt.callLater(field.report)
            if (commitsTyping)
                typingCommit.restart()
        }
        onSelectionStartChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
        onSelectionEndChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
        onCursorPositionChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
        Connections {
            target: root.editor
            function onChanged() {
                field.sync()
                field.refreshMarks()
            }
            function onSelectionRequested() {
                if (root.editor.selectionRole === field.role)
                    field.select(root.editor.selectionStart, root.editor.selectionEnd)
            }
            function onFieldFocusRequested(role) {
                if (role === field.role)
                    root.focusPanel(field, Qt.OtherFocusReason)
            }
        }
        function underlineRows(first, last) {
            const right = width - rightPadding
            if (last.y <= first.y)
                return [{x: first.x, y: first.y + first.height, width: last.x - first.x}]
            const out = [{x: first.x, y: first.y + first.height, width: right - first.x}]
            for (let y = first.y + first.height; out.length < 1000;) {
                const row = positionToRectangle(positionAt(leftPadding, y + 1))
                if (row.y >= last.y || row.y < y - 1 || row.height <= 0)
                    break
                out.push({x: leftPadding, y: row.y + row.height, width: right - leftPadding})
                y = row.y + row.height
            }
            out.push({x: leftPadding, y: last.y + last.height, width: last.x - leftPadding})
            return out
        }
        Repeater {
            model: root.editor.translationMode ? root.editor.tagRuns : []
            delegate: Item {
                id: run
                required property var modelData
                anchors.fill: parent
                z: 2
                Item {
                    id: suggestion
                    readonly property int from: field.role === 1 ? run.modelData.suggestedStart : -1
                    readonly property int to: field.role === 1 ? run.modelData.suggestedEnd : -1
                    readonly property rect first: { field.text; field.width; return field.positionToRectangle(from) }
                    readonly property rect last: { field.text; field.width; return field.positionToRectangle(to) }
                    objectName: field.objectName + "Run" + run.modelData.number + "Suggestion"
                    visible: from >= 0 && to > from && to <= field.length
                    anchors.fill: parent
                    Shape {
                        anchors.fill: parent
                        ShapePath {
                            strokeColor: Theme.accent
                            strokeWidth: 2
                            strokeStyle: ShapePath.DashLine
                            dashPattern: [1, 1]
                            capStyle: ShapePath.FlatCap
                            fillColor: "transparent"
                            PathMultiline {
                                paths: suggestion.visible
                                       ? field.underlineRows(suggestion.first, suggestion.last).map(
                                             r => [Qt.point(r.x, r.y), Qt.point(r.x + r.width, r.y)])
                                       : []
                            }
                        }
                    }
                }
                Item {
                    id: mark
                    readonly property var modelData: run.modelData
                    readonly property int from: field.role === 0 ? modelData.originalStart : modelData.start
                    readonly property int to: field.role === 0 ? modelData.originalEnd : modelData.end
                    readonly property rect first: { field.text; field.width; return field.positionToRectangle(from) }
                    readonly property rect last: { field.text; field.width; return field.positionToRectangle(to) }
                    objectName: field.objectName + "Run" + modelData.number
                    visible: from >= 0 && to > from && to <= field.length
                    anchors.fill: parent
                    Rectangle {
                        objectName: mark.objectName + "Badge"
                        x: mark.first.x - width / 2
                        y: mark.first.y - height / 2
                        width: Math.max(badgeText.implicitWidth + 4, height)
                        height: badgeText.implicitHeight
                        radius: height / 2
                        color: Theme.accent
                        Label {
                            id: badgeText
                            anchors.centerIn: parent
                            text: mark.modelData.number
                            font.pixelSize: Math.max(8, field.font.pixelSize * 0.6)
                            color: Theme.onAccent
                        }
                    }
                    Repeater {
                        model: field.role === 1 ? field.underlineRows(mark.first, mark.last) : []
                        Rectangle {
                            required property var modelData
                            required property int index
                            objectName: mark.objectName + "Underline" + index
                            x: modelData.x
                            y: modelData.y - 1
                            width: modelData.width
                            height: 2
                            color: Theme.accent
                        }
                    }
                }
            }
        }
        readonly property bool spelled: role === (root.editor.translationMode ? 1 : 0)
        property var misspell: ({})
        property int misspellPosition: -1
        property var grammarRanges: []
        property var grammar: ({})
        function refreshMarks() {
            spellMarks.ranges = spelled ? root.app.editorSpellingMarks(role) : []
            grammarRanges = spelled ? root.app.editorGrammarMarks(role) : []
        }
        onSpelledChanged: refreshMarks()
        // Qt Quick text draws straight underlines only, so the wavy one is a Shape.
        Shape {
            id: grammarMarks
            objectName: field.objectName + "Grammar"
            anchors.fill: parent
            z: 1
            visible: rows.length > 0
            readonly property var rows: {
                field.text
                field.width
                field.font
                const out = []
                for (let i = 0; i + 1 < field.grammarRanges.length; i += 2) {
                    const from = field.grammarRanges[i], to = field.grammarRanges[i + 1]
                    if (from < 0 || to <= from || to > field.length)
                        continue
                    for (const r of field.underlineRows(field.positionToRectangle(from), field.positionToRectangle(to)))
                        if (r.width > 0)
                            out.push([r.x, r.y, r.width])
                }
                return out
            }
            ShapePath {
                strokeColor: Theme.grammar
                strokeWidth: 1.2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathMultiline {
                    paths: grammarMarks.rows.map(r => {
                        const points = []
                        for (let x = 0; x <= r[2]; x += 2)
                            points.push(Qt.point(r[0] + x, r[1] - ((x / 2) % 2 === 0 ? 0.5 : 2.5)))
                        return points
                    })
                }
            }
        }
        EditorHighlighter {
            id: spellMarks
            objectName: field.objectName + "Highlighter"
            document: field.textDocument
            colour: Theme.spellcheck
            runs: {
                root.editor.textState
                root.editor.translationMode
                return root.editor.colourRuns(field.role, field.text)
            }
            colours: Theme.syntax
        }
        Connections {
            target: root.app
            function onSpellingChanged() { field.refreshMarks() }
            function onGrammarChanged() { field.refreshMarks() }
        }
        function readMarksAt(position) {
            field.misspellPosition = field.positionAt(position.x, position.y)
            field.grammar = field.spelled ? root.app.editorGrammarAt(field.role, field.misspellPosition) : ({})
            field.misspell = field.spelled && !field.grammar.rule
                ? root.app.editorMisspellAt(field.role, field.misspellPosition) : ({})
        }
        ContextMenu.onRequested: position => {
            root.editor.tagList.close()
            field.readMarksAt(position)
        }
        MenuKey.enabled: root.editor.tagList.open
        MenuKey.onRequested: tagListPopup.openOptions()
        function ignoreGrammarRule() {
            const group = root.app.ignoreGrammarRule(field.grammar.rule)
            if (group < 0)
                return
            const name = field.grammar.groupName
            const groups = root.app.grammarGroups()
            const rules = group < groups.length ? groups[group].rules : 0
            root.notices.post("info", qsTranslate("HikariSub.Spelling", "%1 turned off").arg(name),
                              qsTranslate("HikariSub.Spelling", "Grammar check no longer uses its %n rules. Turn it back on in Settings › Spelling and grammar.", "", rules),
                              {actions: [{text: qsTranslate("HikariSub.Spelling", "Undo"), run: () => root.app.setGrammarGroup(group, true)},
                                         {text: qsTranslate("HikariSub.Spelling", "Grammar settings…"), run: () => root.openSettings("spelling")}]})
        }
        ContextMenu.menu: ShellMenu {
            id: editMenu
            objectName: field.objectName + "Menu"
            readonly property bool grammar: !!field.grammar.rule
            readonly property var suggestions: (grammar ? field.grammar.suggestions : field.misspell.suggestions) || []
            property bool headerShown: false
            Instantiator {
                model: editMenu.grammar ? 1 : 0
                delegate: ShellMenuItem {
                    objectName: field.objectName + "GrammarMessage"
                    iconRole: "spellchecker"
                    enabled: false
                    background: null
                    contentItem: RowLayout {
                        spacing: 8
                        Icon { iconRole: "spellchecker"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 1 }
                        ColumnLayout {
                            spacing: 2
                            Label {
                                text: field.grammar.message || ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.text
                                Layout.maximumWidth: 340
                            }
                            Label {
                                text: qsTranslate("HikariSub.Spelling", "Grammar · %1").arg(field.grammar.groupName || "")
                                textFormat: Text.PlainText
                                color: Theme.muted
                            }
                        }
                    }
                }
                onObjectAdded: (index, object) => {
                    editMenu.insertItem(0, object)
                    editMenu.headerShown = true
                }
                onObjectRemoved: (index, object) => {
                    editMenu.removeItem(object)
                    editMenu.headerShown = false
                }
            }
            Instantiator {
                model: editMenu.suggestions
                delegate: ShellMenuItem {
                    required property string modelData
                    text: modelData
                    font.bold: true
                    onTriggered: editMenu.grammar ? root.app.replaceEditorGrammar(field.role, field.misspellPosition, modelData)
                                                  : root.app.replaceEditorMisspell(field.role, field.misspellPosition, modelData)
                }
                onObjectAdded: (index, object) => editMenu.insertItem(index + (editMenu.headerShown ? 1 : 0), object)
                onObjectRemoved: (index, object) => editMenu.removeItem(object)
            }
            MenuSeparator { visible: editMenu.grammar; height: visible ? implicitHeight : 0 }
            ShellMenuItem {
                iconRole: "hide"
                objectName: field.objectName + "IgnoreRule"
                text: qsTranslate("HikariSub.Spelling", "Ignore this rule")
                detailText: qsTranslate("HikariSub.Spelling", "Turns off %1").arg(field.grammar.groupName || "")
                visible: editMenu.grammar
                height: visible ? implicitHeight : 0
                onTriggered: field.ignoreGrammarRule()
            }
            ShellMenuItem {
                readonly property string word: editMenu.grammar ? (field.grammar.word ? field.grammar.text || "" : "")
                                                                : field.misspell.word || ""
                iconRole: "dictionary-add"
                objectName: field.objectName + "AddWord"
                text: qsTranslate("HikariSub.Spelling", "Add “%1” to dictionary").arg(word)
                visible: word.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    const added = editMenu.grammar ? root.app.addGrammarWord(word) : root.app.addEditorWord(word)
                    if (!added)
                        root.notices.post("error", qsTranslate("HikariSub.Editor", "Add word to dictionary"),
                                          qsTranslate("HikariSub.Editor", "“%1” was not added to the dictionary.").arg(word))
                }
            }
            MenuSeparator {
                visible: editMenu.suggestions.length > 0 || editMenu.grammar || !!field.misspell.word
                height: visible ? implicitHeight : 0
            }
            TextMenuItem {
                kind: TextMenuItem.Undo
                editor: field
                enabled: root.editor.hasLine && !field.readOnly
            }
            TextMenuItem {
                kind: TextMenuItem.Redo
                editor: field
                enabled: root.editor.hasLine && !field.readOnly
            }
            MenuSeparator {}
            TextMenuItem { kind: TextMenuItem.Cut; editor: field }
            TextMenuItem { kind: TextMenuItem.Copy; editor: field }
            TextMenuItem { kind: TextMenuItem.Paste; editor: field }
            TextMenuItem { kind: TextMenuItem.Delete; editor: field }
            MenuSeparator {}
            TextMenuItem { kind: TextMenuItem.SelectAll; editor: field }
            MenuSeparator { visible: !field.readOnly; height: visible ? implicitHeight : 0 }
            LineBreaksMenu {
                shellRoot: root
                prefix: field.objectName
                enabled: !field.readOnly && root.editor.hasLine
            }
            ChangeCaseMenu {
                shellRoot: root
                prefix: field.objectName
                enabled: !field.readOnly && root.editor.hasLine
            }
            ShellMenuItem {
                command: "GLOBAL_SWAP_DIALOGUE_ALTERNATIVES"
                iconRole: "dialogue-swap"
                objectName: field.objectName + "SwapAlternatives"
                text: qsTranslate("HikariSub.Text", "Swap dialogue alternatives")
                enabled: !field.readOnly && root.editor.hasLine
                onTriggered: if (!root.hotkeyGesture(command)) root.app.textCommand("swapAlternatives")
            }
            MenuSeparator {}
            ShellMenuItem {
                command: "GLOBAL_BLUR_AND_GLOW_LAYERS"
                iconRole: "layers"
                objectName: field.objectName + "BlurAndGlowLayers"
                text: qsTranslate("HikariSub.Hotkeys", "Blur and glow layers…")
                enabled: root.editor.hasLine
                keepsMenuOpen: false
                onTriggered: root.openBlurAndGlowLayers()
            }
            ShellMenuItem { command: "GLOBAL_REMOVE_TAGS"; iconRole: "remove-tags"; objectName: field.objectName + "RemoveTags"; action: removeTagsAction }
            MenuSeparator { visible: fieldAiMenu.shown; height: visible ? implicitHeight : 0 }
            InlineAiMenu {
                id: fieldAiMenu
                objectName: field.objectName + "AiMenu"
                controller: root.inlineAi
                editor: true
                shown: root.inlineAi.on && field.role === 0 && root.editor.hasLine
                onChosen: ask => inlineAiBox.begin(ask, true, field)
                onRepeatRequested: root.repeatInlineAi(true, field)
                onSettingsRequested: root.openSettings("ai")
            }
            MenuSeparator { visible: field.spelled; height: visible ? implicitHeight : 0 }
            ShellMenuItem {
                iconRole: "spellchecker"
                objectName: field.objectName + "SpellingOn"
                text: qsTranslate("HikariSub.Editor", "Check spelling")
                checkable: true
                checked: root.app.spellingOn
                keepsMenuOpen: false
                visible: field.spelled
                height: visible ? implicitHeight : 0
                onTriggered: root.app.spellingOn = checked
            }
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: eventPoint => {
                if (!root.app.suggestionsOnDoubleClick || !field.spelled)
                    return
                const position = field.positionAt(eventPoint.position.x, eventPoint.position.y)
                const found = root.app.editorMisspellAt(field.role, position)
                if (found.word) {
                    field.deselect()
                    field.cursorPosition = position
                    fixSuggestions.openFor(field, field.role, position, found.suggestions)
                }
            }
        }
        // Korean IMEs (Microsoft's, ibus-hangul) commit the syllable and then forward the same Enter, so the first
        // Enter or Escape within compositionKeyWindow ms after a composition ends is swallowed.
        readonly property int compositionKeyWindow: 200
        property double compositionEndedAt: -1
        onInputMethodComposingChanged: compositionEndedAt = inputMethodComposing ? -1 : Date.now()
        onActiveFocusChanged: {
            if (!activeFocus) {
                compositionEndedAt = -1
                if (typingCommit.running)
                    field.commitTyping()
            } else if (focusReason !== Qt.ActiveWindowFocusReason && focusReason !== Qt.PopupFocusReason)
                root.lastTextField = field
        }
        onPressed: compositionEndedAt = -1
        function compositionKey(event) {
            const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
            if (mods !== 0 || (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter
                               && event.key !== Qt.Key_Escape))
                return false
            return inputMethodComposing
                || (compositionEndedAt >= 0 && Date.now() - compositionEndedAt <= compositionKeyWindow)
        }
        function endComposition(cancel) {
            if (inputMethodComposing && !cancel)
                Qt.inputMethod.commit()
            if (inputMethodComposing)
                Qt.inputMethod.reset() // Windows' commit() is empty; its reset() commits the composition
            if (cancel)
                tagListPopup.dismiss()
            compositionEndedAt = -1
        }
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape)
                field.compositionEndedAt = -1
        }
        function hotkeyAction(event) {
            const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            if (enter && field.inputMethodComposing)
                return ""
            if (enter && mods === 0)
                return "EDITBOX_COMMIT_GO_NEXT_LINE"
            if (event.key === Qt.Key_Enter && mods === Qt.ControlModifier)
                return "EDITBOX_COMMIT"
            // MUST KILL: actionFor's first argument is a magic int hotkey window (1 Grid, 2 Editor, 3 Video, 4 Audio)
            return root.hotkeys.actionFor(2, event.key, event.modifiers)
        }
        function tabOut(forward) {
            const reason = forward ? Qt.TabFocusReason : Qt.BacktabFocusReason
            const linked = forward ? field.tabNext : field.tabPrevious
            const dock = linked === translationText ? editorDock : linked === lineText ? originalDock : null
            if (dock && dock.isOpen) {
                root.focusPanel(linked, reason)
                return
            }
            const next = field.nextItemInFocusChain(forward)
            if (next && next !== field)
                next.forceActiveFocus(reason)
        }
        // Claimed whatever the platform's undo keys are, or on xcb Ctrl+Y reaches GLOBAL_REDO.
        function undoRedoKey(event) {
            return (event.modifiers & ~Qt.ShiftModifier) === Qt.ControlModifier
                   && (event.key === Qt.Key_Z || (event.key === Qt.Key_Y && !(event.modifiers & Qt.ShiftModifier)))
        }
        Keys.onShortcutOverride: event => event.accepted = field.compositionKey(event) || hotkeyAction(event) !== ""
                                                           || tagListPopup.takesKey(event) || field.undoRedoKey(event)
        Keys.onPressed: event => {
            if (field.compositionKey(event)) {
                field.endComposition(event.key === Qt.Key_Escape)
                event.accepted = true
                return
            }
            field.compositionEndedAt = -1
            if (tagListPopup.key(event)) {
                event.accepted = true
                return
            }
            const ctrl = event.modifiers & Qt.ControlModifier
            const action = hotkeyAction(event)
            const plain = (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
            if (action !== "") {
                root.runEditorHotkey(action, field)
                event.accepted = true
            } else if (plain && field.role === 1 && event.key === Qt.Key_Tab && root.editor.acceptSuggestedRuns()) {
                event.accepted = true
            } else if (plain && field.role === 1 && event.key === Qt.Key_Escape && root.editor.dismissSuggestedRuns()) {
                event.accepted = true
            } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                field.tabOut(event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier))
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                root.editor.discard()
                event.accepted = true
            } else if (ctrl && event.key === Qt.Key_Z && !(event.modifiers & Qt.ShiftModifier)) {
                root.editor.undo()
                event.accepted = true
            } else if (ctrl && (event.key === Qt.Key_Y
                                || (event.key === Qt.Key_Z && (event.modifiers & Qt.ShiftModifier)))) {
                root.editor.redo()
                event.accepted = true
            }
        }
        EditorTagList {
            id: tagListPopup
            field: field
            controller: root.editor.tagList
            enabled: root.editor.showTags && !field.readOnly
        }
    }

    component Panel: FocusScope {
        id: panel
        property string title
        property string accessibleName: title
        default property alias content: body.data
        readonly property real bodyHeight: body.height
        property string dockName
        property size minimumBodySize: Qt.size(0, 0)
        property Item keyTarget: null
        readonly property size minimumSize: Qt.size(Math.ceil(minimumBodySize.width) + 2 * body.anchors.margins,
                                                    Math.ceil(minimumBodySize.height) + 2 * body.anchors.margins)
        function reportMinimumSize() {
            if (dockName.length > 0)
                Docking.setMinimumSize(dockName, minimumSize)
        }
        onMinimumSizeChanged: reportMinimumSize()
        Component.onCompleted: Qt.callLater(reportMinimumSize)
        activeFocusOnTab: false
        clip: true
        Accessible.role: Accessible.Pane
        Accessible.name: accessibleName
        Accessible.description: accessibleName !== title ? title : ""

        Rectangle {
            anchors.fill: parent
            color: panel.palette.base
        }
        Item {
            id: body
            anchors {
                fill: parent
                margins: 4
            }
        }
    }

    DropArea {
        id: tabDropArea
        objectName: "tabDropArea"
        parent: root.contentItem.parent
        anchors.fill: parent
        z: 3
        visible: root.app.tabDragging
        keys: ["application/x-hikari-tab"]
        onDropped: drop => {
            const document = Number(drop.getDataAsString("application/x-hikari-tab"))
            const at = documentTabBar.mapFromItem(tabDropArea, drop.x, drop.y)
            const index = documentTabBar.dropIndexAt(at.x, at.y, document)
            if (index >= 0)
                root.app.moveTab(document, root.shellWindow, index)
            drop.acceptProposedAction()
        }
    }
    function tabDropIndex(x, y) {
        const at = documentTabBar.mapFromGlobal(x, y)
        return documentTabBar.dropIndexAt(at.x, at.y, 0)
    }

    // Before the docking area so KDDW's hit test (last child wins) finds the panels; non-file drags are refused for its Wayland drop indicators.
    DropArea {
        id: fileDropArea
        objectName: "dropArea"
        anchors.fill: parent
        z: 2
        onEntered: drag => {
            if (!drag.hasUrls)
                drag.accepted = false
        }
        onDropped: drop => {
            if (!drop.hasUrls)
                return
            drop.accept(Qt.CopyAction)
            const result = root.app.openDropped(drop.urls)
            if (result.kind === "subtitles")
                root.openSubtitles(result.path)
            else if (result.kind === "video")
                root.openSingleVideo(result.path)
            else if (result.kind === "session")
                root.openSessionFile(result.path)
            else if (result.kind === "files" && result.rows.length > 0)
                closeReview.review(result.rows)
        }
    }

    // Wayland: KDDW's QtQuick frontend takes panel drops only through a DropArea with dropAreaCpp, which it gives floating windows alone.
    DropArea {
        id: panelDropArea
        objectName: "panelDropArea"
        anchors.fill: dockingArea
        z: 1
        visible: Qt.platform.pluginName.startsWith("wayland")
        property QtObject dropAreaCpp: null
        Component.onCompleted: Qt.callLater(() => dropAreaCpp = Docking.mainDropArea(dockingArea.uniqueName))
    }

    HomePage {
        id: homePage
        objectName: "homePage"
        app: root.app
        shellWindow: root.shellWindow
        anchors.fill: dockingArea
        z: 1.5
        visible: root.windowHome
        spaceAbove: documentTabBar.height
        onNewRequested: root.app.addPage()
        onOpenRequested: openAction.trigger()
        onOpenFolderRequested: openFolderAction.trigger()
        onProjectChosen: path => root.app.openFolder(path, root.shellWindow)
        onOpenVideoRequested: openVideoAction.trigger()
        onRestoreRequested: sessionWindows.load()
        offer: sessionWindows.offer
        onAutosavesRequested: autosaves.show()
        whatsNew: root.whatsNew.releases.length > 0 ? whatsNewCard : null
        onRecentChosen: path => { if (!root.app.revealRecent(path)) root.openSubtitles(path) }
        onVisibleChanged: visible ? Qt.callLater(takeFocus) : Qt.callLater(root.focusAfterHome)
        Component.onCompleted: if (visible) Qt.callLater(takeFocus)
    }
    Component {
        id: whatsNewCard
        WhatsNewCard {
            whatsNew: root.whatsNew
            smallFont: homePage.smallFont
        }
    }
    function focusAfterHome() {
        if (homePage.visible || (root.activeFocusItem && root.activeFocusItem !== root.contentItem))
            return
        const shown = root.panels.filter(p => p.visible)
        const next = shown.includes(gridPanel) ? gridPanel : shown[0]
        if (next)
            root.focusPanel(next, Qt.OtherFocusReason)
    }

    KDDW.DockingArea {
        id: dockingArea
        objectName: "dockingArea"
        anchors.fill: parent
        uniqueName: root.engineName("Classic")
        affinities: root.dockAffinities
        enabled: !homePage.visible

        PanelDock {
            id: videoDock
            objectName: "videoDock"
            uniqueName: root.engineName("Video")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Video", "Video")
            Panel {
                id: videoPanel
                anchors.fill: parent
                objectName: "videoPanel"
                dockName: root.engineName("Video")
                // MUST KILL: 72 is a magic height for two tool rail buttons
                minimumBodySize: Qt.size(videoTransport.implicitWidth,
                                         videoControls.implicitHeight + videoToolBar.lineHeight + videoControls.spacing + 72
                                         - (videoToolBar.visible ? videoToolBar.implicitHeight + videoControls.spacing : 0))
                title: qsTranslate("HikariSub.Video", "Video")
                function panelAction(event) {
                    const action = root.hotkeys.actionFor(3, event.key, event.modifiers)
                    return action === "VIDEO_FULLSCREEN_BAR" ? "" : action
                }
                Keys.onShortcutOverride: event => event.accepted = (event.key === Qt.Key_Escape && root.visualTools.escapable)
                                                  || root.visualTools.takesKey(event.key, event.modifiers)
                                                  || videoPanel.panelAction(event) !== ""
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape && root.visualTools.escape()) {
                        event.accepted = true
                        return
                    }
                    if (root.visualTools.takesKey(event.key, event.modifiers)) {
                        root.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)
                        event.accepted = true
                        return
                    }
                    const action = videoPanel.panelAction(event)
                    if (action !== "") {
                        root.runVideoHotkey(action)
                        event.accepted = true
                    } else if (event.key === Qt.Key_F) {
                        root.videoFullscreen.toggle(0)
                        event.accepted = true
                    } else if (root.videoView.key(event.key, event.modifiers)) {
                        event.accepted = true
                    } else if (root.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                        event.accepted = true
                    }
                }
                Keys.onReleased: event => event.accepted = root.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)
                MenuKey.onRequested: videoContextMenu.openAt(root.videoView.cursorIn(visualOverlay))

                AssociationOffer {
                    video: root.video
                    anchors { left: videoStageDock.left; right: parent.right; top: parent.top }
                    z: 1
                }
                VideoIndexingProgress {
                    video: root.video
                    anchors { left: videoStageDock.left; right: parent.right; top: parent.top }
                    z: 1
                }
                VisualToolRail {
                    id: visualRail
                    tools: root.visualTools
                    keyOf: symbol => root.boundKeys(symbol, 3)
                    anchors { left: parent.left; top: parent.top; bottom: videoControls.top }
                }
                Item {
                    id: videoStageDock
                    anchors { left: visualRail.visible ? visualRail.right : parent.left; right: parent.right
                              top: parent.top; bottom: videoControls.top }
                }
                Item {
                    id: videoStage
                    objectName: "videoStage"
                    parent: root.videoStageInFullscreen ? videoFullscreenWindow.stage : videoStageDock
                    anchors.fill: parent
                    clip: true
                VideoPresenter {
                    id: presenter
                    objectName: "videoPresenter"
                    visible: root.video.hasVideo
                    anchors.fill: parent
                    videoRect: root.visualTools.frameRect
                    sourceRect: root.visualTools.sourceRect
                    Component.onCompleted: root.video.attachPresenter(presenter)
                }
                VisualOverlay {
                    id: visualOverlay
                    anchors.fill: presenter
                    tools: root.visualTools
                    focusTarget: root.videoFullscreen.active ? videoFullscreenWindow.keyTarget : videoPanel
                    panelHeight: root.videoFullscreen.active ? videoFullscreenWindow.pinnedPanelHeight : videoControls.height
                    view: root.videoView
                    cursorOverride: root.videoFullscreen.active && videoFullscreenWindow.cursorHidden ? Qt.BlankCursor : -1
                    onContextMenuRequested: (x, y) => {
                        if (root.videoFullscreen.active)
                            videoFullscreenWindow.openMenuAt(Qt.point(x, y))
                        else
                            videoContextMenu.openAt(Qt.point(x, y))
                    }
                    onFullScreenRequested: root.videoDoubleClick()
                    onPointerMoved: (x, y) => {
                        if (root.videoFullscreen.active)
                            videoFullscreenWindow.pointerMoved(visualOverlay.mapToItem(null, x, y))
                    }
                }
                VideoZoomFrame {
                    anchors.fill: presenter
                    view: root.videoView
                }
                VideoTemplateBadge {
                    id: templateBadge
                    x: presenter.x + 8
                    y: presenter.y + 8
                    visible: root.templateEditor.videoPreview && !root.nodeEffects.videoPreview && root.video.hasVideo
                    controller: root.templateEditor
                }
                VideoNodeBadge {
                    id: nodeBadge
                    x: presenter.x + 8
                    y: presenter.y + 8
                    visible: root.nodeEffects.videoPreview && root.video.hasVideo
                    controller: root.nodeEffects
                }
                VideoRenderCost {
                    anchors.fill: presenter
                    visible: root.videoView.renderCost && root.video.hasVideo
                    cost: presenter.renderCost
                    textTop: templateBadge.visible ? templateBadge.height + 12 : nodeBadge.visible ? nodeBadge.height + 12 : 8
                }
                VisualLastChange {
                    anchors { top: parent.top; right: parent.right; margins: 14 }
                    tools: root.visualTools
                    // MUST KILL: activeFamily 10 is a magic int for Family::Hydra
                    visible: hasChange && root.video.hasVideo && root.visualTools.activeFamily === 10
                }
                }
                VideoContextMenu {
                    id: videoContextMenu
                    shell: root
                    function openAt(point) {
                        at = point
                        popup(visualOverlay, point)
                    }
                    onOpenVideoRequested: videoDialog.show()
                    onOpenSubtitlesRequested: openDialog.show()
                    onAspectRatioRequested: root.lazy(aspectRatioDialogLoader).openAtCursor(visualOverlay)
                }
                Label {
                    anchors.centerIn: videoStageDock
                    visible: !root.video.hasVideo
                    text: root.video.status
                }
                ColumnLayout {
                    id: videoControls
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    spacing: 2
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => root.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
                    }
                VisualToolBar {
                    id: videoToolBar
                    Layout.fillWidth: true
                    dialogs: !videoFullscreenWindow.visible
                    settings: root.app.settings
                    Layout.minimumWidth: 0
                    tools: root.visualTools
                }
                Slider {
                    objectName: "videoSlider"
                    id: videoSeekBar
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0, root.video.frameCount - 1)
                    stepSize: 1
                    Binding on value {
                        when: !videoSeekBar.pressed
                        value: Math.max(0, root.video.frame)
                        restoreMode: Binding.RestoreNone
                    }
                    enabled: root.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTranslate("HikariSub.Video", "Video position")
                    onMoved: root.video.seekBarMoved(Math.round(value), pressed)
                    onPressedChanged: if (!pressed) root.video.seekBarReleased()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 24
                    spacing: 10
                    Label {
                        objectName: "videoTimes"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: root.video.times
                        font.features: { "tnum": 1 }
                        color: root.video.keyframeShown ? Theme.warning : palette.windowText
                        Accessible.name: qsTranslate("HikariSub.Video", "Video times")
                        Accessible.description: qsTranslate("HikariSub.Video", "Frame time; frame number; frames from the line’s start frame; "
                                                     + "milliseconds from the line’s start and end")
                        HoverHandler { id: timesHover }
                        ToolTip.visible: timesHover.hovered && text.length > 0
                        ToolTip.text: Accessible.description
                    }
                    VisualPointerReadout {
                        tools: root.visualTools
                    }
                }
                RowLayout {
                    id: videoTransport
                    Layout.fillWidth: true
                    IconToolButton {
                        objectName: "previousFile"
                        iconRole: "media-previous-file"
                        text: qsTranslate("HikariSub.Video", "Previous file")
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Previous file"), "VIDEO_PREVIOUS_FILE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_PREVIOUS_FILE", 3, "bitmap")) videoFileQuestion.ask(false)
                    }
                    IconToolButton {
                        objectName: "playPause"
                        iconRole: root.video.playing ? "media-pause" : "media-play"
                        text: root.video.playing ? qsTranslate("HikariSub.Video", "Pause") : qsTranslate("HikariSub.Video", "Play")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Play or pause"), "VIDEO_PLAY_PAUSE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_PLAY_PAUSE", 3, "bitmap")) root.video.togglePlay()
                    }
                    IconToolButton {
                        objectName: "playActualLine"
                        iconRole: "play-line"
                        text: qsTranslate("HikariSub.Video", "Play line")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Play the current line"), "GLOBAL_PLAY_ACTUAL_LINE", 0)
                        Accessible.name: qsTranslate("HikariSub.Video", "Play the current line")
                        onClicked: {
                            if (root.hotkeyGesture("GLOBAL_PLAY_ACTUAL_LINE", 0, "bitmap"))
                                return
                            root.focusPanel(lineText, Qt.OtherFocusReason)
                            root.video.playActualLine()
                        }
                    }
                    IconToolButton {
                        objectName: "stopVideo"
                        iconRole: "media-stop"
                        text: qsTranslate("HikariSub.Video", "Stop")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Stop"), "VIDEO_STOP", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_STOP", 3, "bitmap")) root.video.stop()
                    }
                    IconToolButton {
                        objectName: "nextFile"
                        iconRole: "media-next-file"
                        text: qsTranslate("HikariSub.Video", "Next file")
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Next file"), "VIDEO_NEXT_FILE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_NEXT_FILE", 3, "bitmap")) videoFileQuestion.ask(true)
                    }
                    IconToolButton {
                        objectName: "previousFrame"
                        iconRole: "frame-previous"
                        text: qsTranslate("HikariSub.Video", "Previous frame")
                        enabled: root.video.hasVideo && root.video.frame > 0
                        focusPolicy: Qt.NoFocus
                        onClicked: root.video.stepFrames(-1)
                    }
                    Label {
                        objectName: "videoStatus"
                        text: root.video.status
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    IconToolButton {
                        objectName: "nextFrame"
                        iconRole: "frame-next"
                        text: qsTranslate("HikariSub.Video", "Next frame")
                        enabled: root.video.hasVideo && root.video.frame + 1 < root.video.frameCount
                        focusPolicy: Qt.NoFocus
                        onClicked: root.video.stepFrames(1)
                    }
                    VideoVolumeSlider {
                        view: root.videoView
                        hasVideo: root.video.hasVideo
                    }
                }
                }
            }
        }

        PanelDock {
            id: audioDock
            objectName: "audioDock"
            uniqueName: root.engineName("Audio")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Audio", "Audio")
            Panel {
                id: audioPanel
                anchors.fill: parent
                objectName: "audioPanel"
                dockName: root.engineName("Audio")
                minimumBodySize: audioButtons.visible
                                 ? Qt.size(audioButtons.foldedWidth, 64 + audioScroll.implicitHeight + 4 + audioButtons.implicitHeight)
                                 : Qt.size(audioStatus.implicitWidth, audioStatus.implicitHeight)
                title: qsTranslate("HikariSub.Audio", "Audio")
                AudioTapBar {
                    id: audioTapBar
                    audio: root.audio
                    visible: root.audio.loaded && root.audio.tapOn
                    height: visible ? implicitHeight : 0
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                }
                AudioDisplay {
                    id: audioDisplay
                    objectName: "audioDisplay"
                    controller: root.audio
                    visible: root.audio.loaded
                    focus: true
                    clip: true
                    anchors { left: parent.left; right: audioSliderColumn.visible ? audioSliderColumn.left : parent.right
                              top: audioTapBar.bottom; bottom: audioScroll.top }
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTranslate("HikariSub.Audio", "Audio display")
                    Accessible.description: root.audio.status
                }
                Item {
                    id: audioTapLane
                    objectName: "audioTapLane"
                    readonly property var spans: root.audio.tapSpans
                    visible: audioTapBar.visible
                    clip: true
                    x: audioDisplay.x
                    y: audioDisplay.y + 22
                    width: audioDisplay.width
                    height: 22
                    Repeater {
                        model: audioTapLane.spans
                        delegate: Item {
                            id: span
                            required property var modelData
                            required property int index
                            readonly property real gapStart: index > 0 ? audioTapLane.spans[index - 1].x2 : x
                            objectName: "audioTapSpan" + index
                            x: modelData.x1
                            width: Math.max(1, modelData.x2 - modelData.x1)
                            height: parent.height
                            Canvas {
                                objectName: "audioTapGap"
                                readonly property color ink: Theme.highContrast ? Theme.text : Theme.muted
                                visible: span.x - span.gapStart > 2
                                x: span.gapStart - span.x
                                width: Math.max(0, span.x - span.gapStart)
                                height: parent.height
                                onInkChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = ink
                                    ctx.lineWidth = 1.5
                                    ctx.beginPath()
                                    for (let d = -height; d < width; d += 6) {
                                        ctx.moveTo(d, height)
                                        ctx.lineTo(d + height, 0)
                                    }
                                    ctx.stroke()
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.highContrast && !span.modelData.held ? Theme.field
                                     : Color.transparent(Theme.accent, span.modelData.held || Theme.highContrast ? 0.9 : 0.5)
                                border.width: Theme.highContrast ? 1 : 0
                                border.color: Theme.accent
                            }
                            Label {
                                objectName: "audioTapSpanText"
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - 2)
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: span.modelData.text
                                textFormat: Text.PlainText
                                font.bold: true
                                color: span.modelData.held ? Theme.onAccent : Theme.text
                            }
                        }
                    }
                }
                ScrollBar {
                    id: audioScroll
                    objectName: "audioScroll"
                    visible: root.audio.loaded
                    orientation: Qt.Horizontal
                    policy: ScrollBar.AlwaysOn
                    focusPolicy: Qt.NoFocus
                    anchors { left: parent.left; right: audioSliderColumn.visible ? audioSliderColumn.left : parent.right
                              bottom: audioButtons.top; bottomMargin: visible ? 4 : 0 }
                    height: visible ? implicitHeight : 0
                    size: root.audio.scrollRange > 0 ? Math.min(1, root.audio.scrollPage / root.audio.scrollRange) : 1
                    background: Rectangle {
                        implicitHeight: 6
                        radius: 3
                        color: Theme.field
                        border.color: Theme.line
                    }
                    Binding on position {
                        when: !audioScroll.pressed
                        value: root.audio.scrollRange > 0 ? root.audio.scrollPosition / root.audio.scrollRange : 0
                    }
                    onPositionChanged: {
                        if (pressed)
                            root.audio.setScrollPosition(Math.round(position * root.audio.scrollRange))
                    }
                }
                RowLayout {
                    id: audioSliderColumn
                    objectName: "audioSliderColumn"
                    visible: root.audio.loaded && audioPanel.shows("sliders")
                    anchors { right: parent.right; top: parent.top; bottom: audioButtons.top; bottomMargin: 4 }
                    width: visible ? implicitWidth : 0
                    spacing: 2
                    component ColumnSlider: Slider {
                        property string tip
                        orientation: Qt.Vertical
                        stepSize: 1
                        focusPolicy: Qt.NoFocus
                        Layout.fillHeight: true
                        Layout.preferredWidth: 20
                        Accessible.name: tip
                        ToolTip.text: tip
                        ToolTip.visible: hovered || pressed
                    }
                    Rectangle {
                        Layout.fillHeight: true
                        implicitWidth: 1
                        color: Theme.line
                    }
                    ColumnSlider {
                        objectName: "audioColumnHorizontal"
                        tip: qsTranslate("HikariSub.Audio", "Horizontal stretching")
                        from: 100
                        to: 0
                        value: root.audio.horizontalZoom
                        onMoved: root.audio.setHorizontalZoom(Math.round(value))
                    }
                    ColumnLayout {
                        Layout.fillHeight: true
                        spacing: 2
                        RowLayout {
                            spacing: 2
                            ColumnSlider {
                                objectName: "audioColumnVertical"
                                tip: qsTranslate("HikariSub.Audio", "Vertical stretching")
                                from: 1
                                to: 100
                                value: root.audio.verticalZoom
                                onMoved: root.audio.setVerticalZoom(Math.round(value))
                            }
                            ColumnSlider {
                                objectName: "audioColumnVolume"
                                tip: qsTranslate("HikariSub.Audio", "Volume")
                                from: 1
                                to: 100
                                value: root.audio.volume
                                onMoved: root.audio.setVolume(Math.round(value))
                            }
                        }
                        ToolButton {
                            objectName: "audioColumnLink"
                            Layout.fillWidth: true
                            implicitHeight: 26
                            focusPolicy: Qt.NoFocus
                            checkable: true
                            checked: root.audio.linked
                            display: AbstractButton.IconOnly
                            text: qsTranslate("HikariSub.Audio", "Link the volume and stretch sliders")
                            contentItem: Item {
                                Icon {
                                    anchors.centerIn: parent
                                    iconRole: "link"
                                }
                            }
                            Accessible.name: text
                            ToolTip.text: text
                            ToolTip.visible: hovered
                            onToggled: {
                                root.audio.setLinked(checked)
                                checked = Qt.binding(() => root.audio.linked)
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 2
                                color: Theme.accent
                                visible: parent.checked
                            }
                        }
                    }
                }
                Label {
                    id: audioStatus
                    objectName: "audioStatus"
                    anchors.centerIn: parent
                    visible: !root.audio.loaded
                    text: root.audio.status
                }
                function audioTip(text, symbol, alt) {
                    let key = root.boundKeys(symbol, 4)
                    if (alt)
                        key = root.eitherKey(key, root.boundKeys(alt, 4))
                    let tip = key.length ? text + " (" + key + ")" : text
                    tip += "\n" + qsTranslate("HikariSub.Audio", "Shift+click to set a shortcut")
                    if (alt)
                        tip += "\n" + qsTranslate("HikariSub.Audio", "Ctrl+click to set a second shortcut")
                    return tip
                }
                readonly property var playArounds: [
                    { id: "before", symbol: "AUDIO_PLAY_500MS_BEFORE", icon: "play-before-start",
                      tip: qsTranslate("HikariSub.Audio", "Play 500 ms before the start time"),
                      label: qsTranslate("HikariSub.Audio", "Play 500 ms before the start") },
                    { id: "first", symbol: "AUDIO_PLAY_500MS_FIRST", icon: "play-after-start",
                      tip: qsTranslate("HikariSub.Audio", "Play 500 ms after the start time"),
                      label: qsTranslate("HikariSub.Audio", "Play the first 500 ms") },
                    { id: "last", symbol: "AUDIO_PLAY_500MS_LAST", icon: "play-before-end",
                      tip: qsTranslate("HikariSub.Audio", "Play 500 ms before the end time"),
                      label: qsTranslate("HikariSub.Audio", "Play the last 500 ms") },
                    { id: "after", symbol: "AUDIO_PLAY_500MS_AFTER", icon: "play-after-end",
                      tip: qsTranslate("HikariSub.Audio", "Play 500 ms after the end time"),
                      label: qsTranslate("HikariSub.Audio", "Play 500 ms after the end") },
                    { id: "toEnd", symbol: "AUDIO_PLAY_TO_END", icon: "play-to-end",
                      tip: qsTranslate("HikariSub.Audio", "Play to the end"),
                      label: qsTranslate("HikariSub.Audio", "Play to the end") },
                    { id: "beforeMark", symbol: "AUDIO_PLAY_BEFORE_MARK", icon: "play-before-mark",
                      tip: qsTranslate("HikariSub.Audio", "Play before the mark"),
                      label: qsTranslate("HikariSub.Audio", "Play before the mark") },
                    { id: "afterMark", symbol: "AUDIO_PLAY_AFTER_MARK", icon: "play-after-mark",
                      tip: qsTranslate("HikariSub.Audio", "Play after the mark"),
                      label: qsTranslate("HikariSub.Audio", "Play after the mark") }
                ]
                property string playAroundSetting: root.app.settings.value("audio.playAround")
                readonly property var playAround: playArounds.find(p => p.symbol === playAroundSetting) ?? playArounds[0]
                function choosePlayAround(symbol) {
                    root.app.settings.setValue("audio.playAround", symbol)
                    root.audio.runHotkey(symbol)
                }
                property bool autoCommit: root.app.settings.value("audio.autoCommit")
                property bool nextLineOnCommit: root.app.settings.value("audio.nextLineOnCommit")
                Connections {
                    target: root.app.settings
                    function onChanged(id) {
                        if (id === "audio.autoCommit")
                            audioPanel.autoCommit = root.app.settings.value(id)
                        else if (id === "audio.nextLineOnCommit")
                            audioPanel.nextLineOnCommit = root.app.settings.value(id)
                        else if (id === "audio.playAround")
                            audioPanel.playAroundSetting = root.app.settings.value(id)
                        else if (id === "audio.panelControls")
                            audioPanel.panelControls = root.app.settings.value(id)
                    }
                }
                property var panelControls: root.app.settings.value("audio.panelControls")
                readonly property var extraOrder: ["beforeMark", "afterMark", "before", "first", "last", "after", "toEnd",
                                                   "autoCommit", "nextCommit", "goto"]
                readonly property var playExtras: ["beforeMark", "afterMark", "before", "first", "last", "after", "toEnd"]
                readonly property var classicControls: extraOrder.concat(["sliders"])
                readonly property var defaultControls: ["playAround"]
                function shows(id) { return panelControls.indexOf(id) >= 0 }
                function setShown(id, on) {
                    const list = panelControls.filter(c => c !== id)
                    if (on)
                        list.push(id)
                    root.app.settings.setValue("audio.panelControls", list)
                }
                function arrange(list) { root.app.settings.setValue("audio.panelControls", list) }
                function play(id) { return playArounds.find(p => p.id === id) }
                readonly property var markPlays: ["beforeMark", "afterMark"].map(play)
                readonly property var timePlays: ["before", "first", "last", "after", "toEnd"].map(play)
                function setCommitOption(id, on) {
                    root.app.settings.setValue(id, on)
                    audioDisplay.forceActiveFocus()
                }
                readonly property string magnificationText: {
                    const m = root.audio.magnification
                    //: The audio panel's zoom readout; %1 is the magnification
                    return qsTranslate("HikariSub.Audio", "%1×").arg(m >= 10 ? Math.round(m) : m.toFixed(1))
                }
                AbstractButton {
                    id: audioBackToLine
                    objectName: "audioBackToLine"
                    readonly property int side: root.audio.lineOutOfView
                    visible: root.audio.loaded && side !== 0 && !root.audio.cursorIsPlayback
                    focusPolicy: Qt.NoFocus
                    z: 1
                    x: side < 0 ? audioDisplay.x + 8 : audioDisplay.x + audioDisplay.width - width - 8
                    y: audioDisplay.y + 26
                    leftPadding: 6
                    rightPadding: 8
                    topPadding: 0
                    bottomPadding: 0
                    implicitHeight: 26
                    implicitWidth: leftPadding + rightPadding + chipRow.implicitWidth
                    text: qsTranslate("HikariSub.Audio", "Back to line")
                    Accessible.name: text
                    ToolTip.text: audioPanel.audioTip(qsTranslate("HikariSub.Audio", "Go to selection"), "AUDIO_GOTO", "")
                    ToolTip.visible: hovered
                    background: Rectangle {
                        radius: height / 2
                        color: audioBackToLine.down ? Theme.select : audioBackToLine.hovered ? Theme.raised : Theme.panel
                        border.color: Theme.control
                    }
                    contentItem: RowLayout {
                        id: chipRow
                        spacing: 6
                        Icon {
                            iconRole: "go-to-selection"
                            mirrored: audioBackToLine.side < 0
                        }
                        Label {
                            text: audioBackToLine.text
                            font.bold: true
                        }
                        Label {
                            readonly property string key: root.boundKeys("AUDIO_GOTO", 4)
                            visible: key.length > 0
                            text: key
                            color: Theme.muted
                        }
                    }
                    onClicked: {
                        if (root.hotkeyGesture("AUDIO_GOTO", 4, true, false))
                            return
                        root.audio.goToSelection()
                    }
                }
                RowLayout {
                    id: audioButtons
                    objectName: "audioButtons"
                    visible: root.audio.loaded
                    height: visible ? implicitHeight : 0
                    spacing: 2
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: visible ? 2 : 0 }
                    readonly property bool folded: width < transportWidth(shownExtras)
                                                           + (root.audio.karaoke ? audioSwitches.modesWidth + 26 + spacing
                                                                                 : audioSwitches.fullWidth)
                                                           + audioTail.unfoldedWidth + 3 * spacing
                    readonly property bool togglesFolded: folded
                        || width < transportWidth(shownExtras) + audioSwitches.fullWidth + audioTail.unfoldedWidth + 3 * spacing
                    readonly property bool splitArrowFolded: folded
                        || root.audio.karaoke && width < transportWidth(shownExtras) + audioSwitches.modesWidth
                                                        + audioSwitches.splitArrowWidth + 26 + spacing
                                                        + audioTail.unfoldedWidth + 3 * spacing
                    readonly property real foldedWidth: transportWidth([]) + audioTail.unfoldedWidth + 26 + 3 * spacing
                    readonly property var shownExtras: audioPanel.extraOrder.filter(id => audioPanel.shows(id))
                    readonly property var kept: {
                        if (!folded)
                            return shownExtras
                        const room = width - audioTail.unfoldedWidth - 26 - 3 * spacing
                        let k = shownExtras.length
                        while (k > 0 && transportWidth(shownExtras.slice(0, k)) > room)
                            --k
                        return shownExtras.slice(0, k)
                    }
                    function keeps(id) { return kept.indexOf(id) >= 0 }
                    function foldsOut(id) { return audioPanel.shows(id) && !keeps(id) }
                    // MUST KILL: transportWidth hardcodes the row's item widths (button 26, split button 39, separator 5)
                    function transportWidth(extras) {
                        const around = audioPanel.shows("playAround")
                        const marks = extras.filter(id => id === "beforeMark" || id === "afterMark").length
                        const plays = extras.filter(id => audioPanel.playExtras.indexOf(id) >= 0).length
                        const widths = [26, 26, 26, 26, 26, 5]
                        if (around)
                            widths.push(39)
                        if (marks > 0 && around)
                            widths.push(5)
                        if (plays > marks && (around || marks > 0))
                            widths.push(5)
                        for (let i = 0; i < extras.length; ++i)
                            widths.push(26)
                        if (around || plays > 0)
                            widths.push(5)
                        widths.push(26, 26, 39)
                        return widths.reduce((a, b) => a + b, 0) + (widths.length - 1) * spacing
                    }
                    component AudioButton: ToolButton {
                        id: audioButton
                        property string symbol: ""
                        property string altSymbol: ""
                        property string tip: ""
                        property string iconRole: ""
                        signal run()
                        focusPolicy: Qt.NoFocus
                        padding: 5
                        display: AbstractButton.IconOnly
                        contentItem: Icon {
                            iconRole: audioButton.iconRole
                        }
                        implicitHeight: 26
                        implicitWidth: 26
                        Accessible.name: tip
                        ToolTip.text: symbol.length ? audioPanel.audioTip(tip, symbol, altSymbol) : tip
                        ToolTip.visible: hovered
                        onClicked: {
                            if (symbol.length && root.hotkeyGesture(symbol, 4, true, altSymbol.length > 0))
                                return
                            run()
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 2
                            color: Theme.accent
                            visible: audioButton.checked
                        }
                    }
                    component AudioSegment: AudioButton {
                        id: segment
                        property bool divider: false
                        Accessible.role: Accessible.RadioButton
                        Accessible.checkable: true
                        Accessible.checked: checked
                        background: Rectangle {
                            color: segment.checked ? Theme.select : segment.hovered ? Theme.raised : "transparent"
                            Rectangle {
                                width: 1
                                height: parent.height
                                color: Theme.line
                                visible: segment.divider
                            }
                        }
                    }
                    component AudioSwitch: Rectangle {
                        default property alias segments: switchRow.data
                        implicitWidth: switchRow.implicitWidth + 2
                        implicitHeight: 28
                        color: "transparent"
                        radius: 3
                        border.color: Theme.control
                        Accessible.role: Accessible.Grouping
                        Row {
                            id: switchRow
                            anchors { fill: parent; margins: 1 }
                        }
                    }
                    component AudioArrow: Canvas {
                        implicitWidth: 8
                        implicitHeight: 8
                        readonly property color ink: enabled ? Theme.text : Theme.disabled
                        onInkChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = ink
                            ctx.lineWidth = 1
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            ctx.beginPath()
                            ctx.moveTo(1.5, 3)
                            ctx.lineTo(4, 5.5)
                            ctx.lineTo(6.5, 3)
                            ctx.stroke()
                        }
                    }
                    component AudioSplitButton: Row {
                        id: split
                        property string name
                        property string iconRole
                        property string symbol
                        property string altSymbol
                        property string tip
                        property string menuTip
                        property Menu menu
                        property string faceText: tip
                        property real arrowWidth: 13
                        property bool arrowShown: true
                        property alias checkable: splitFace.checkable
                        property alias checked: splitFace.checked
                        default property alias marks: splitFace.data
                        signal run()
                        signal toggled()
                        AudioButton {
                            id: splitFace
                            objectName: split.name
                            text: split.faceText
                            iconRole: split.iconRole
                            symbol: split.symbol
                            altSymbol: split.altSymbol
                            tip: split.tip
                            onRun: split.run()
                            onToggled: split.toggled()
                        }
                        ToolButton {
                            objectName: split.name + "Arrow"
                            visible: split.arrowShown
                            focusPolicy: Qt.NoFocus
                            implicitWidth: split.arrowWidth
                            implicitHeight: 26
                            padding: 0
                            down: split.menu !== null && split.menu.visible
                            display: AbstractButton.IconOnly
                            text: split.menuTip
                            Accessible.name: split.menuTip
                            Accessible.role: Accessible.ButtonMenu
                            ToolTip.text: split.menuTip
                            ToolTip.visible: hovered
                            contentItem: Item {
                                AudioArrow {
                                    anchors.centerIn: parent
                                }
                            }
                            onClicked: split.menu.popup(splitFace, 0, splitFace.height)
                        }
                    }
                    component CommitMark: Rectangle {
                        property string glyph
                        width: Math.max(9, markText.implicitWidth + 2)
                        height: 9
                        radius: 2
                        color: Theme.accent
                        Label {
                            id: markText
                            anchors.centerIn: parent
                            text: parent.glyph
                            color: Theme.onAccent
                            font.pixelSize: 8
                            font.bold: true
                        }
                    }
                    component AudioSeparator: Rectangle {
                        implicitWidth: 1
                        implicitHeight: 18
                        Layout.leftMargin: 2
                        Layout.rightMargin: 2
                        color: Theme.line
                    }
                    component TapOptionsMenu: ShellMenu {
                        id: tapMenu
                        property string names: "audio"
                        property bool tapRow: true
                        ShellMenuItem {
                            objectName: tapMenu.names + "TapStart"
                            visible: tapMenu.tapRow
                            height: tapMenu.tapRow ? implicitHeight : 0
                            iconRole: "karaoke-tap"
                            text: qsTranslate("HikariSub.Audio", "Tap to time")
                            command: "AUDIO_TAP"
                            checkable: true
                            keepsMenuOpen: false
                            checked: root.audio.tapOn
                            onTriggered: {
                                checked = Qt.binding(() => root.audio.tapOn)
                                if (!root.hotkeyGesture("AUDIO_TAP", 4, true, false)) root.audio.toggleTap()
                            }
                        }
                        MenuSeparator {
                            visible: tapMenu.tapRow
                            height: tapMenu.tapRow ? implicitHeight : 0
                        }
                        MenuHeading {
                            text: qsTranslate("HikariSub.Audio", "Each syllable")
                        }
                        MenuRadioItem {
                            objectName: tapMenu.names + "TapHold"
                            text: qsTranslate("HikariSub.Audio", "Hold the key: releasing it leaves a gap")
                            checked: root.audio.tapHold
                            onTriggered: {
                                root.audio.setTapHold(true)
                                checked = Qt.binding(() => root.audio.tapHold)
                            }
                        }
                        MenuRadioItem {
                            objectName: tapMenu.names + "TapPress"
                            text: qsTranslate("HikariSub.Audio", "Press the key: no gaps")
                            checked: !root.audio.tapHold
                            onTriggered: {
                                root.audio.setTapHold(false)
                                checked = Qt.binding(() => !root.audio.tapHold)
                            }
                        }
                        MenuSeparator {}
                        ShellMenuItem {
                            objectName: tapMenu.names + "OnsetSnap"
                            text: qsTranslate("HikariSub.Audio", "Snap to vocal onsets")
                            checkable: true
                            checked: root.audio.onsetSnap
                            onToggled: {
                                root.audio.setOnsetSnap(checked)
                                checked = Qt.binding(() => root.audio.onsetSnap)
                            }
                        }
                        MenuSeparator {}
                        ShellMenu {
                            id: speedMenu
                            objectName: tapMenu.names + "TapSpeedMenu"
                            iconRole: "playback-speed"
                            //: The Tap menu's Speed submenu; the current speed follows it
                            title: qsTranslate("HikariSub.Audio", "Speed")
                            detailText: qsTranslate("HikariSub.Audio", "%1%").arg(root.audio.speedPercent)
                            Instantiator {
                                model: [25, 33, 50, 75, 100]
                                delegate: MenuRadioItem {
                                    required property int modelData
                                    objectName: tapMenu.names + "TapSpeed" + modelData
                                    text: qsTranslate("HikariSub.Audio", "%1%").arg(modelData)
                                    checked: root.audio.speedPercent === modelData
                                    onTriggered: {
                                        root.audio.setSpeedPercent(modelData)
                                        checked = Qt.binding(() => root.audio.speedPercent === modelData)
                                    }
                                }
                                onObjectAdded: (index, object) => speedMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => speedMenu.removeItem(object)
                            }
                        }
                        ShellMenuItem {
                            objectName: tapMenu.names + "TapCalibrate"
                            iconRole: "tap-calibrate"
                            //: The Tap menu's item; the tap latency follows it
                            text: qsTranslate("HikariSub.Audio", "Calibrate latency…")
                            detailText: qsTranslate("HikariSub.Audio", "%1 ms").arg(root.audio.tapLatency)
                            onTriggered: root.lazy(tapCalibrationWindowLoader).openWindow()
                        }
                    }
                    component PlayAroundItem: ShellMenuItem {
                        id: playItem
                        property string symbol
                        property string label
                        text: label
                        command: symbol
                        checkable: true
                        checked: audioPanel.playAround.symbol === symbol
                        keepsMenuOpen: false
                        onTriggered: {
                            checked = Qt.binding(() => audioPanel.playAround.symbol === playItem.symbol)
                            if (root.hotkeyGesture(symbol, 4, true, false))
                                return
                            audioPanel.choosePlayAround(symbol)
                        }
                    }

                    RowLayout {
                        id: audioTransport
                        objectName: "audioTransport"
                        spacing: 2
                        AudioButton {
                            objectName: "audioPrevious"; text: qsTranslate("HikariSub.Audio", "Previous line")
                            iconRole: "audio-previous-line"
                            symbol: "AUDIO_PREVIOUS"; altSymbol: "AUDIO_PREVIOUS_ALT"; tip: qsTranslate("HikariSub.Audio", "Play the previous line")
                            onRun: root.audio.previousLine()
                        }
                        AudioButton {
                            objectName: "audioPlay"; text: qsTranslate("HikariSub.Audio", "Play")
                            iconRole: "audio-play"
                            symbol: "AUDIO_PLAY"; altSymbol: "AUDIO_PLAY_ALT"; tip: qsTranslate("HikariSub.Audio", "Play the current syllable / line")
                            onRun: root.audio.runHotkey("AUDIO_PLAY")
                        }
                        AudioButton {
                            objectName: "audioPlayLine"; text: qsTranslate("HikariSub.Audio", "Play line")
                            iconRole: "play-line"
                            symbol: "AUDIO_PLAY_LINE"; altSymbol: "AUDIO_PLAY_LINE_ALT"; tip: qsTranslate("HikariSub.Audio", "Play the current line")
                            onRun: root.audio.runHotkey("AUDIO_PLAY_LINE")
                        }
                        AudioButton {
                            objectName: "audioStop"; text: qsTranslate("HikariSub.Audio", "Stop")
                            iconRole: "media-stop"
                            symbol: "AUDIO_STOP"; tip: qsTranslate("HikariSub.Audio", "Stop playback")
                            onRun: root.audio.runHotkey("AUDIO_STOP")
                        }
                        AudioButton {
                            objectName: "audioNext"; text: qsTranslate("HikariSub.Audio", "Next line")
                            iconRole: "audio-next-line"
                            symbol: "AUDIO_NEXT"; altSymbol: "AUDIO_NEXT_ALT"; tip: qsTranslate("HikariSub.Audio", "Play the next line")
                            onRun: root.audio.nextLine()
                        }
                        AudioSeparator {}
                        AudioSplitButton {
                            name: "audioPlayAround"
                            visible: audioPanel.shows("playAround")
                            iconRole: audioPanel.playAround.icon
                            symbol: audioPanel.playAround.symbol
                            tip: audioPanel.playAround.tip
                            menuTip: qsTranslate("HikariSub.Audio", "Play around")
                            menu: audioPlayAroundMenu
                            onRun: root.audio.runHotkey(audioPanel.playAround.symbol)
                        }
                        ShellMenu {
                            id: audioPlayAroundMenu
                            objectName: "audioPlayAroundMenu"
                            PlayAroundItem {
                                objectName: "audioPlay500Before"; iconRole: "play-before-start"
                                symbol: "AUDIO_PLAY_500MS_BEFORE"; label: qsTranslate("HikariSub.Audio", "Play 500 ms before the start")
                            }
                            PlayAroundItem {
                                objectName: "audioPlay500First"; iconRole: "play-after-start"
                                symbol: "AUDIO_PLAY_500MS_FIRST"; label: qsTranslate("HikariSub.Audio", "Play the first 500 ms")
                            }
                            PlayAroundItem {
                                objectName: "audioPlay500Last"; iconRole: "play-before-end"
                                symbol: "AUDIO_PLAY_500MS_LAST"; label: qsTranslate("HikariSub.Audio", "Play the last 500 ms")
                            }
                            PlayAroundItem {
                                objectName: "audioPlay500After"; iconRole: "play-after-end"
                                symbol: "AUDIO_PLAY_500MS_AFTER"; label: qsTranslate("HikariSub.Audio", "Play 500 ms after the end")
                            }
                            PlayAroundItem {
                                objectName: "audioPlayToEnd"; iconRole: "play-to-end"
                                symbol: "AUDIO_PLAY_TO_END"; label: qsTranslate("HikariSub.Audio", "Play to the end")
                            }
                            MenuSeparator {}
                            PlayAroundItem {
                                objectName: "audioPlayBeforeMark"; iconRole: "play-before-mark"
                                symbol: "AUDIO_PLAY_BEFORE_MARK"; label: qsTranslate("HikariSub.Audio", "Play before the mark")
                            }
                            PlayAroundItem {
                                objectName: "audioPlayAfterMark"; iconRole: "play-after-mark"
                                symbol: "AUDIO_PLAY_AFTER_MARK"; label: qsTranslate("HikariSub.Audio", "Play after the mark")
                            }
                        }
                        component ExtraPlay: AudioButton {
                            required property var modelData
                            objectName: "audioExtra" + modelData.id[0].toUpperCase() + modelData.id.slice(1)
                            visible: audioButtons.keeps(modelData.id)
                            iconRole: modelData.icon
                            text: modelData.label
                            symbol: modelData.symbol
                            tip: modelData.tip
                            onRun: root.audio.runHotkey(modelData.symbol)
                        }
                        AudioSeparator {
                            visible: audioPanel.shows("playAround") && (audioButtons.keeps("beforeMark") || audioButtons.keeps("afterMark"))
                        }
                        Repeater {
                            model: audioPanel.markPlays
                            delegate: ExtraPlay {}
                        }
                        AudioSeparator {
                            visible: (audioPanel.shows("playAround") || audioButtons.keeps("beforeMark") || audioButtons.keeps("afterMark"))
                                     && audioPanel.timePlays.some(p => audioButtons.keeps(p.id))
                        }
                        Repeater {
                            model: audioPanel.timePlays
                            delegate: ExtraPlay {}
                        }
                        AudioSeparator {
                            visible: audioPanel.shows("playAround") || audioPanel.playArounds.some(p => audioButtons.keeps(p.id))
                        }
                        AudioButton {
                            objectName: "audioLeadIn"; text: qsTranslate("HikariSub.Audio", "In")
                            iconRole: "lead-in"
                            symbol: "AUDIO_LEAD_IN"; tip: qsTranslate("HikariSub.Audio", "Add lead-in to the active line")
                            onRun: root.audio.leadIn()
                        }
                        AudioButton {
                            objectName: "audioLeadOut"; text: qsTranslate("HikariSub.Audio", "Out")
                            iconRole: "lead-out"
                            symbol: "AUDIO_LEAD_OUT"; tip: qsTranslate("HikariSub.Audio", "Add lead-out to the active line")
                            onRun: root.audio.leadOut()
                        }
                        AudioSplitButton {
                            name: "audioCommit"
                            iconRole: "commit"
                            symbol: "AUDIO_COMMIT"; altSymbol: "AUDIO_COMMIT_ALT"
                            tip: qsTranslate("HikariSub.Audio", "Apply changes")
                            menuTip: qsTranslate("HikariSub.Audio", "Options for applying changes")
                            menu: audioCommitMenu
                            onRun: root.audio.commit()
                            Row {
                                anchors { right: parent.right; bottom: parent.bottom; rightMargin: 1; bottomMargin: 1 }
                                spacing: 1
                                CommitMark {
                                    objectName: "audioAutoCommitMark"
                                    glyph: "A"
                                    visible: audioPanel.autoCommit && !audioButtons.keeps("autoCommit")
                                }
                                CommitMark {
                                    objectName: "audioNextCommitMark"
                                    glyph: "↓"
                                    visible: audioPanel.nextLineOnCommit && !audioButtons.keeps("nextCommit")
                                }
                            }
                        }
                        ShellMenu {
                            id: audioCommitMenu
                            objectName: "audioCommitMenu"
                            ShellMenuItem {
                                objectName: "audioCommitItem"
                                iconRole: "commit"
                                text: qsTranslate("HikariSub.Audio", "Apply changes")
                                shortcutText: root.eitherKey(root.boundKeys("AUDIO_COMMIT", 4), root.boundKeys("AUDIO_COMMIT_ALT", 4))
                                onTriggered: if (!root.hotkeyGesture("AUDIO_COMMIT", 4, true, true)) root.audio.commit()
                            }
                            MenuSeparator {}
                            ShellMenuItem {
                                id: audioAutoCommit
                                objectName: "audioAutoCommit"
                                iconRole: "auto-commit"
                                text: qsTranslate("HikariSub.Audio", "Apply changes automatically")
                                checkable: true
                                checked: audioPanel.autoCommit
                                onToggled: {
                                    audioPanel.setCommitOption("audio.autoCommit", checked)
                                    checked = Qt.binding(() => audioPanel.autoCommit)
                                }
                            }
                            ShellMenuItem {
                                id: audioNextCommit
                                objectName: "audioNextCommit"
                                iconRole: "next-after-commit"
                                text: qsTranslate("HikariSub.Audio", "Go to the next line after applying")
                                checkable: true
                                checked: audioPanel.nextLineOnCommit
                                onToggled: {
                                    audioPanel.setCommitOption("audio.nextLineOnCommit", checked)
                                    checked = Qt.binding(() => audioPanel.nextLineOnCommit)
                                }
                            }
                        }
                        AudioButton {
                            objectName: "audioExtraAutoCommit"
                            visible: audioButtons.keeps("autoCommit")
                            iconRole: "auto-commit"
                            text: qsTranslate("HikariSub.Audio", "Apply changes automatically")
                            tip: qsTranslate("HikariSub.Audio", "Apply changes automatically")
                            checkable: true
                            checked: audioPanel.autoCommit
                            onToggled: {
                                audioPanel.setCommitOption("audio.autoCommit", checked)
                                checked = Qt.binding(() => audioPanel.autoCommit)
                            }
                        }
                        AudioButton {
                            objectName: "audioExtraNextCommit"
                            visible: audioButtons.keeps("nextCommit")
                            iconRole: "next-after-commit"
                            text: qsTranslate("HikariSub.Audio", "Go to the next line after applying")
                            tip: qsTranslate("HikariSub.Audio", "Go to the next line after applying")
                            checkable: true
                            checked: audioPanel.nextLineOnCommit
                            onToggled: {
                                audioPanel.setCommitOption("audio.nextLineOnCommit", checked)
                                checked = Qt.binding(() => audioPanel.nextLineOnCommit)
                            }
                        }
                        AudioButton {
                            objectName: "audioExtraGoto"
                            visible: audioButtons.keeps("goto")
                            iconRole: "go-to-selection"
                            text: qsTranslate("HikariSub.Audio", "Go to selection")
                            symbol: "AUDIO_GOTO"
                            tip: qsTranslate("HikariSub.Audio", "Go to selection")
                            onRun: root.audio.goToSelection()
                        }
                    }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        id: audioSwitches
                        objectName: "audioSwitches"
                        visible: !audioButtons.folded
                        spacing: 2
                        // MUST KILL: modesWidth and fullWidth hardcode the switches' and toggles' widths
                        readonly property real modesWidth: 2 * 54 + 2 * spacing + (root.audio.karaoke ? 26 + 39 + 2 * spacing : 0)
                        readonly property real splitArrowWidth: root.audio.karaoke ? 9 : 0
                        readonly property real fullWidth: modesWidth + splitArrowWidth + 5 + 3 * 26 + 4 * spacing
                        AudioSwitch {
                            AudioSegment {
                                objectName: "audioLineMode"
                                iconRole: "audio-line-mode"
                                text: qsTranslate("HikariSub.Audio", "Line")
                                tip: qsTranslate("HikariSub.Audio", "Line timing")
                                checked: !root.audio.karaoke
                                onRun: if (root.audio.karaoke) root.audio.toggleKaraoke()
                            }
                            AudioSegment {
                                objectName: "audioKaraoke"
                                iconRole: "karaoke"
                                divider: true
                                text: qsTranslate("HikariSub.Audio", "Karaoke")
                                tip: qsTranslate("HikariSub.Audio", "Karaoke")
                                checked: root.audio.karaoke
                                onRun: if (!root.audio.karaoke) root.audio.toggleKaraoke()
                            }
                        }
                        AudioSplitButton {
                            name: "audioKaraokeSplit"
                            visible: root.audio.karaoke
                            iconRole: "karaoke-split"
                            faceText: qsTranslate("HikariSub.Audio", "Auto split")
                            checkable: true
                            checked: root.audio.karaokeSplitMode
                            tip: qsTranslate("HikariSub.Audio", "Enable / Disable automatic splitting of syllables")
                            menuTip: qsTranslate("HikariSub.Audio", "Syllable commands")
                            menu: audioKaraokeMenu
                            arrowWidth: 9
                            arrowShown: !audioButtons.splitArrowFolded
                            onToggled: {
                                root.audio.toggleKaraokeSplitMode()
                                checked = Qt.binding(() => root.audio.karaokeSplitMode)
                            }
                        }
                        ShellMenu {
                            id: audioKaraokeMenu
                            objectName: "audioKaraokeMenu"
                            ShellMenuItem {
                                objectName: "audioKaraokeSplitAtCursor"
                                iconRole: "karaoke-split"
                                text: qsTranslate("HikariSub.Audio", "Split syllable at the cursor")
                                command: "AUDIO_KARAOKE_SPLIT"
                                onTriggered: root.audio.splitSyllableAtCursor()
                            }
                            ShellMenuItem {
                                objectName: "audioKaraokeKf"
                                iconRole: "karaoke"
                                text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k", "\\kf")
                                command: "AUDIO_KARAOKE_KF"
                                onTriggered: root.audio.switchLongSyllablesToKf()
                            }
                        }
                        AudioSplitButton {
                            name: "audioTap"
                            visible: root.audio.karaoke
                            iconRole: "karaoke-tap"
                            symbol: "AUDIO_TAP"
                            tip: qsTranslate("HikariSub.Audio", "Tap to time")
                            menuTip: qsTranslate("HikariSub.Audio", "Tap options")
                            menu: audioTapMenu
                            checked: root.audio.tapOn
                            onRun: root.audio.toggleTap()
                        }
                        TapOptionsMenu {
                            id: audioTapMenu
                            objectName: "audioTapMenu"
                        }
                        AudioSwitch {
                            AudioSegment {
                                objectName: "audioWaveformMode"
                                iconRole: "audio-waveform"
                                text: qsTranslate("HikariSub.Audio", "Waveform")
                                tip: qsTranslate("HikariSub.Audio", "Waveform mode")
                                checked: !root.audio.spectrumOn
                                onRun: { root.audio.setSpectrumOn(false); audioDisplay.forceActiveFocus() }
                            }
                            AudioSegment {
                                objectName: "audioSpectrumMode"
                                iconRole: "spectrum"
                                divider: true
                                text: qsTranslate("HikariSub.Audio", "Spectrum")
                                tip: qsTranslate("HikariSub.Audio", "Spectrum mode")
                                checked: root.audio.spectrumOn
                                onRun: { root.audio.setSpectrumOn(true); audioDisplay.forceActiveFocus() }
                            }
                        }
                        AudioSeparator {
                            visible: !audioButtons.togglesFolded
                        }
                        AudioButton {
                            objectName: "audioAutoScroll"
                            visible: !audioButtons.togglesFolded
                            iconRole: "auto-scroll"
                            text: qsTranslate("HikariSub.Audio", "Auto-scroll")
                            checkable: true
                            checked: root.audio.autoScroll
                            tip: qsTranslate("HikariSub.Audio", "Auto-scroll to the active line")
                            onToggled: { root.audio.setAutoScroll(checked); audioDisplay.forceActiveFocus() }
                        }
                        AudioButton {
                            objectName: "audioSyncVideo"
                            visible: !audioButtons.togglesFolded
                            iconRole: "audio-video-sync"
                            text: qsTranslate("HikariSub.Audio", "Sync with video")
                            checkable: true
                            checked: root.audio.syncVideo
                            tip: qsTranslate("HikariSub.Audio", "Sync the video and audio cursors")
                            onToggled: root.audio.setSyncVideo(checked)
                        }
                        AudioButton {
                            objectName: "audioSpectrumNonLinear"
                            visible: !audioButtons.togglesFolded
                            iconRole: "spectrum-nonlinear"
                            text: qsTranslate("HikariSub.Audio", "Speech")
                            checkable: true
                            checked: root.audio.spectrumNonLinear
                            tip: qsTranslate("HikariSub.Audio", "Speech spectrum")
                            onToggled: { root.audio.setSpectrumNonLinear(checked); audioDisplay.forceActiveFocus() }
                        }
                    }
                    RowLayout {
                        id: audioTail
                        spacing: 2
                        readonly property real unfoldedWidth: 5 + audioZoomButton.implicitWidth + 2 * 26 + 3 * spacing
                        AudioSeparator {}
                        AudioButton {
                            id: audioMoreButton
                            objectName: "audioMore"
                            iconRole: "menu-more"
                            text: qsTranslate("HikariSub.Audio", "More audio options")
                            tip: text
                            down: audioMoreMenu.visible
                            onRun: audioMoreMenu.popup(audioMoreButton, 0, audioMoreButton.height)
                            ShellMenu {
                                id: audioMoreMenu
                                objectName: "audioMoreMenu"
                                readonly property bool playsFold: audioPanel.playExtras.some(id => audioButtons.foldsOut(id))
                                readonly property bool commitFolds: ["autoCommit", "nextCommit", "goto"].some(id => audioButtons.foldsOut(id))
                                component FoldedItem: ShellMenuItem {
                                    checkable: true
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedRadio: MenuRadioItem {
                                    visible: audioButtons.folded
                                    height: audioButtons.folded ? implicitHeight : 0
                                }
                                component FoldedSeparator: MenuSeparator {
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedToggle: ShellMenuItem {
                                    checkable: true
                                    visible: audioButtons.togglesFolded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedToggleSeparator: MenuSeparator {
                                    visible: audioButtons.togglesFolded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedPlay: ShellMenuItem {
                                    property var play
                                    objectName: "audioMoreExtra" + play.id[0].toUpperCase() + play.id.slice(1)
                                    visible: audioButtons.foldsOut(play.id)
                                    height: visible ? implicitHeight : 0
                                    iconRole: play.icon
                                    text: play.label
                                    command: play.symbol
                                    onTriggered: if (!root.hotkeyGesture(play.symbol, 4, true, false)) root.audio.runHotkey(play.symbol)
                                }
                                FoldedPlay { play: audioPanel.play("beforeMark") }
                                FoldedPlay { play: audioPanel.play("afterMark") }
                                FoldedPlay { play: audioPanel.play("before") }
                                FoldedPlay { play: audioPanel.play("first") }
                                FoldedPlay { play: audioPanel.play("last") }
                                FoldedPlay { play: audioPanel.play("after") }
                                FoldedPlay { play: audioPanel.play("toEnd") }
                                MenuSeparator {
                                    visible: audioMoreMenu.playsFold && audioMoreMenu.commitFolds
                                    height: visible ? implicitHeight : 0
                                }
                                ShellMenuItem {
                                    objectName: "audioMoreExtraAutoCommit"
                                    visible: audioButtons.foldsOut("autoCommit")
                                    height: visible ? implicitHeight : 0
                                    iconRole: "auto-commit"
                                    text: qsTranslate("HikariSub.Audio", "Apply changes automatically")
                                    checkable: true
                                    checked: audioPanel.autoCommit
                                    onToggled: {
                                        audioPanel.setCommitOption("audio.autoCommit", checked)
                                        checked = Qt.binding(() => audioPanel.autoCommit)
                                    }
                                }
                                ShellMenuItem {
                                    objectName: "audioMoreExtraNextCommit"
                                    visible: audioButtons.foldsOut("nextCommit")
                                    height: visible ? implicitHeight : 0
                                    iconRole: "next-after-commit"
                                    text: qsTranslate("HikariSub.Audio", "Go to the next line after applying")
                                    checkable: true
                                    checked: audioPanel.nextLineOnCommit
                                    onToggled: {
                                        audioPanel.setCommitOption("audio.nextLineOnCommit", checked)
                                        checked = Qt.binding(() => audioPanel.nextLineOnCommit)
                                    }
                                }
                                ShellMenuItem {
                                    objectName: "audioMoreExtraGoto"
                                    visible: audioButtons.foldsOut("goto")
                                    height: visible ? implicitHeight : 0
                                    iconRole: "go-to-selection"
                                    text: qsTranslate("HikariSub.Audio", "Go to selection")
                                    command: "AUDIO_GOTO"
                                    onTriggered: if (!root.hotkeyGesture("AUDIO_GOTO", 4, true, false)) root.audio.goToSelection()
                                }
                                MenuSeparator {
                                    visible: audioButtons.folded && (audioMoreMenu.playsFold || audioMoreMenu.commitFolds)
                                    height: visible ? implicitHeight : 0
                                }
                                FoldedRadio {
                                    objectName: "audioMoreLineTiming"
                                    text: qsTranslate("HikariSub.Audio", "Line timing")
                                    checked: !root.audio.karaoke
                                    onTriggered: {
                                        checked = Qt.binding(() => !root.audio.karaoke)
                                        if (root.audio.karaoke) root.audio.toggleKaraoke()
                                    }
                                }
                                FoldedRadio {
                                    objectName: "audioMoreKaraoke"
                                    text: qsTranslate("HikariSub.Audio", "Karaoke")
                                    checked: root.audio.karaoke
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.karaoke)
                                        if (!root.audio.karaoke) root.audio.toggleKaraoke()
                                    }
                                }
                                FoldedItem {
                                    objectName: "audioMoreKaraokeSplit"
                                    visible: audioButtons.folded && root.audio.karaoke
                                    iconRole: "karaoke-split"
                                    text: qsTranslate("HikariSub.Audio", "Auto split")
                                    checkable: true
                                    checked: root.audio.karaokeSplitMode
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.karaokeSplitMode)
                                        root.audio.toggleKaraokeSplitMode()
                                    }
                                }
                                FoldedItem {
                                    objectName: "audioMoreKaraokeSplitAtCursor"
                                    visible: audioButtons.splitArrowFolded && root.audio.karaoke
                                    checkable: false
                                    iconRole: "karaoke-split"
                                    text: qsTranslate("HikariSub.Audio", "Split syllable at the cursor")
                                    command: "AUDIO_KARAOKE_SPLIT"
                                    onTriggered: root.audio.splitSyllableAtCursor()
                                }
                                FoldedItem {
                                    objectName: "audioMoreKaraokeKf"
                                    visible: audioButtons.splitArrowFolded && root.audio.karaoke
                                    checkable: false
                                    iconRole: "karaoke"
                                    text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k", "\\kf")
                                    command: "AUDIO_KARAOKE_KF"
                                    onTriggered: root.audio.switchLongSyllablesToKf()
                                }
                                FoldedItem {
                                    objectName: "audioMoreTap"
                                    visible: audioButtons.folded && root.audio.karaoke
                                    iconRole: "karaoke-tap"
                                    text: qsTranslate("HikariSub.Audio", "Tap to time")
                                    command: "AUDIO_TAP"
                                    checkable: true
                                    keepsMenuOpen: false
                                    checked: root.audio.tapOn
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.tapOn)
                                        root.audio.toggleTap()
                                    }
                                }
                                TapOptionsMenu {
                                    objectName: "audioMoreTapOptions"
                                    names: "audioMore"
                                    tapRow: false
                                    iconRole: "karaoke-tap"
                                    title: qsTranslate("HikariSub.Audio", "Tap options")
                                    shown: audioButtons.folded && root.audio.karaoke
                                }
                                FoldedSeparator {}
                                FoldedRadio {
                                    objectName: "audioMoreWaveform"
                                    text: qsTranslate("HikariSub.Audio", "Waveform")
                                    checked: !root.audio.spectrumOn
                                    onTriggered: {
                                        checked = Qt.binding(() => !root.audio.spectrumOn)
                                        root.audio.setSpectrumOn(false)
                                    }
                                }
                                FoldedRadio {
                                    objectName: "audioMoreSpectrum"
                                    text: qsTranslate("HikariSub.Audio", "Spectrum")
                                    checked: root.audio.spectrumOn
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.spectrumOn)
                                        root.audio.setSpectrumOn(true)
                                    }
                                }
                                FoldedToggle {
                                    objectName: "audioMoreSpeechSpectrum"
                                    iconRole: "spectrum-nonlinear"
                                    text: qsTranslate("HikariSub.Audio", "Speech spectrum")
                                    checked: root.audio.spectrumNonLinear
                                    onToggled: {
                                        root.audio.setSpectrumNonLinear(checked)
                                        checked = Qt.binding(() => root.audio.spectrumNonLinear)
                                    }
                                }
                                FoldedToggleSeparator {}
                                ShellMenuItem {
                                    objectName: "audioMoreGotoStart"
                                    iconRole: "video-to-start-time"
                                    text: qsTranslate("HikariSub.Audio", "Go to selection start")
                                    command: "AUDIO_GOTO_START"
                                    onTriggered: if (!root.hotkeyGesture("AUDIO_GOTO_START", 4, true, false)) root.audio.goToSelectionStart()
                                }
                                ShellMenuItem {
                                    objectName: "audioMoreGotoEnd"
                                    iconRole: "video-to-end-time"
                                    text: qsTranslate("HikariSub.Audio", "Go to selection end")
                                    command: "AUDIO_GOTO_END"
                                    onTriggered: if (!root.hotkeyGesture("AUDIO_GOTO_END", 4, true, false)) root.audio.goToSelectionEnd()
                                }
                                FoldedToggleSeparator {}
                                FoldedToggle {
                                    objectName: "audioMoreAutoScroll"
                                    iconRole: "auto-scroll"
                                    text: qsTranslate("HikariSub.Audio", "Auto-scroll")
                                    checked: root.audio.autoScroll
                                    onToggled: {
                                        root.audio.setAutoScroll(checked)
                                        checked = Qt.binding(() => root.audio.autoScroll)
                                    }
                                }
                                FoldedToggle {
                                    objectName: "audioMoreSyncVideo"
                                    iconRole: "audio-video-sync"
                                    text: qsTranslate("HikariSub.Audio", "Sync with video")
                                    checked: root.audio.syncVideo
                                    onToggled: {
                                        root.audio.setSyncVideo(checked)
                                        checked = Qt.binding(() => root.audio.syncVideo)
                                    }
                                }
                            }
                        }
                        ToolButton {
                            id: audioZoomButton
                            objectName: "audioZoom"
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 26
                            leftPadding: 4
                            rightPadding: 6
                            down: audioZoomMenu.visible
                            text: qsTranslate("HikariSub.Audio", "Zoom")
                            Accessible.name: text + " " + audioPanel.magnificationText
                            ToolTip.text: qsTranslate("HikariSub.Audio", "Zoom: horizontal and vertical stretching")
                            ToolTip.visible: hovered
                            contentItem: RowLayout {
                                spacing: 4
                                Icon {
                                    iconRole: "zoom"
                                }
                                Label {
                                    objectName: "audioZoomReadout"
                                    text: audioPanel.magnificationText
                                    font.features: ({ "tnum": 1 })
                                }
                            }
                            onClicked: audioZoomMenu.popup(audioZoomButton, 0, audioZoomButton.height)
                            ShellMenu {
                                id: audioZoomMenu
                                objectName: "audioZoomMenu"
                                AudioSliderRow {
                                    sliderName: "audioHorizontalZoom"
                                    text: qsTranslate("HikariSub.Audio", "Horizontal")
                                    from: 100
                                    to: 0
                                    value: root.audio.horizontalZoom
                                    readout: audioPanel.magnificationText
                                    onMoved: v => root.audio.setHorizontalZoom(v)
                                }
                                AudioSliderRow {
                                    sliderName: "audioVerticalZoom"
                                    text: qsTranslate("HikariSub.Audio", "Vertical")
                                    from: 1
                                    to: 100
                                    value: root.audio.verticalZoom
                                    readout: root.audio.verticalZoom
                                    onMoved: v => root.audio.setVerticalZoom(v)
                                }
                                ShellMenuItem {
                                    id: audioLink
                                    objectName: "audioLink"
                                    iconRole: "link"
                                    text: qsTranslate("HikariSub.Audio", "Link the volume and stretch sliders")
                                    checkable: true
                                    checked: root.audio.linked
                                    onToggled: {
                                        root.audio.setLinked(checked)
                                        checked = Qt.binding(() => root.audio.linked)
                                    }
                                }
                                MenuSeparator {}
                                ShellMenuItem {
                                    objectName: "audioZoomReset"
                                    iconRole: "zoom-reset"
                                    text: qsTranslate("HikariSub.Audio", "Reset zoom")
                                    onTriggered: root.audio.resetZoom()
                                }
                                Label {
                                    leftPadding: 12
                                    rightPadding: 12
                                    topPadding: 2
                                    bottomPadding: 4
                                    color: Theme.muted
                                    font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                                    text: qsTranslate("HikariSub.Audio", "Shift+wheel and Ctrl+wheel on the waveform zoom too")
                                }
                            }
                        }
                        AudioButton {
                            id: audioOutputButton
                            objectName: "audioOutputButton"
                            iconRole: root.audio.volume > 66 ? "volume" : root.audio.volume > 20 ? "volume-low" : "volume-quiet"
                            text: qsTranslate("HikariSub.Audio", "Volume and output device")
                            //: The audio panel's speaker button; %1 is the volume (1 to 100), %2 the device audio plays through now
                            tip: (root.app.audioOutputName.length
                                  ? qsTranslate("HikariSub.Audio", "Volume: %1\nOutput device: %2").arg(root.audio.volume, root.app.audioOutputName)
                                  : qsTranslate("HikariSub.Audio", "Volume: %1").arg(root.audio.volume))
                                 //: The speaker button's tooltip, while the audio plays slowed; %1 is the speed in percent
                                 + (root.audio.speedPercent !== 100
                                    ? "\n" + qsTranslate("HikariSub.Audio", "Speed: %1%").arg(root.audio.speedPercent) : "")
                            down: audioOutputMenu.visible
                            onRun: audioOutputMenu.popup(audioOutputButton, 0, audioOutputButton.height)
                            CommitMark {
                                objectName: "audioSpeedMark"
                                anchors { right: parent.right; bottom: parent.bottom; rightMargin: 1; bottomMargin: 1 }
                                glyph: root.audio.speedPercent
                                visible: root.audio.speedPercent !== 100
                            }
                            // High-resolution wheels and touchpads send parts of a 120 notch (or pixels only), so deltas add up to whole notches.
                            WheelHandler {
                                id: speakerWheel
                                property real rest: 0
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: event => {
                                    let delta = event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
                                    if (delta === 0)
                                        delta = 8 * (event.pixelDelta.y !== 0 ? event.pixelDelta.y : -event.pixelDelta.x)
                                    if (delta === 0)
                                        return
                                    if (delta * speakerWheel.rest < 0)
                                        speakerWheel.rest = 0
                                    speakerWheel.rest += delta
                                    const notches = Math.trunc(speakerWheel.rest / 120)
                                    if (notches === 0)
                                        return
                                    speakerWheel.rest -= 120 * notches
                                    root.audio.setVolume(Math.max(1, Math.min(100, root.audio.volume + 5 * notches)))
                                }
                            }
                            AudioOutputMenu {
                                id: audioOutputMenu
                                objectName: "audioOutputMenu"
                                app: root.app
                                audio: root.audio
                                slowerKey: root.boundKeys("AUDIO_SPEED_DOWN", 4)
                                fasterKey: root.boundKeys("AUDIO_SPEED_UP", 4)
                                onSettingsRequested: root.openSettings("audio")
                            }
                        }
                        AudioButton {
                            id: audioCustomiseButton
                            objectName: "audioCustomise"
                            iconRole: "settings"
                            text: qsTranslate("HikariSub.Audio", "Customise the audio panel")
                            tip: text
                            down: audioCustomiseMenu.visible
                            onRun: audioCustomiseMenu.popup(audioCustomiseButton, 0, audioCustomiseButton.height)
                            ShellMenu {
                                id: audioCustomiseMenu
                                objectName: "audioCustomiseMenu"
                                component ShownItem: ShellMenuItem {
                                    property string control
                                    checkable: true
                                    checked: audioPanel.shows(control)
                                    onToggled: {
                                        audioPanel.setShown(control, checked)
                                        checked = Qt.binding(() => audioPanel.shows(control))
                                    }
                                }
                                component ShownPlay: ShownItem {
                                    property var play
                                    control: play.id
                                    objectName: "audioCustomise" + play.id[0].toUpperCase() + play.id.slice(1)
                                    iconRole: play.icon
                                    text: play.label
                                    command: play.symbol
                                }
                                Label {
                                    leftPadding: 12
                                    rightPadding: 12
                                    topPadding: 4
                                    bottomPadding: 2
                                    color: Theme.muted
                                    font.bold: true
                                    text: qsTranslate("HikariSub.Audio", "Show on the panel")
                                }
                                ShownItem {
                                    objectName: "audioCustomisePlayAround"
                                    control: "playAround"
                                    iconRole: audioPanel.playAround.icon
                                    text: qsTranslate("HikariSub.Audio", "Play around button")
                                }
                                MenuSeparator {}
                                ShownPlay { play: audioPanel.play("beforeMark") }
                                ShownPlay { play: audioPanel.play("afterMark") }
                                ShownPlay { play: audioPanel.play("before") }
                                ShownPlay { play: audioPanel.play("first") }
                                ShownPlay { play: audioPanel.play("last") }
                                ShownPlay { play: audioPanel.play("after") }
                                ShownPlay { play: audioPanel.play("toEnd") }
                                MenuSeparator {}
                                ShownItem {
                                    objectName: "audioCustomiseAutoCommit"
                                    control: "autoCommit"
                                    iconRole: "auto-commit"
                                    text: qsTranslate("HikariSub.Audio", "Apply changes automatically")
                                }
                                ShownItem {
                                    objectName: "audioCustomiseNextCommit"
                                    control: "nextCommit"
                                    iconRole: "next-after-commit"
                                    text: qsTranslate("HikariSub.Audio", "Go to the next line after applying")
                                }
                                ShownItem {
                                    objectName: "audioCustomiseGoto"
                                    control: "goto"
                                    iconRole: "go-to-selection"
                                    text: qsTranslate("HikariSub.Audio", "Go to selection")
                                    command: "AUDIO_GOTO"
                                }
                                MenuSeparator {}
                                ShownItem {
                                    objectName: "audioCustomiseSliders"
                                    control: "sliders"
                                    iconRole: "link"
                                    text: qsTranslate("HikariSub.Audio", "Zoom and volume sliders")
                                }
                                MenuSeparator {}
                                ShellMenuItem {
                                    objectName: "audioCustomiseClassic"
                                    iconRole: "layout-restore"
                                    text: qsTranslate("HikariSub.Audio", "Classic arrangement")
                                    onTriggered: audioPanel.arrange(audioPanel.classicControls)
                                }
                                ShellMenuItem {
                                    objectName: "audioCustomiseDefault"
                                    iconRole: "layout-reset"
                                    text: qsTranslate("HikariSub.Audio", "Default arrangement")
                                    onTriggered: audioPanel.arrange(audioPanel.defaultControls)
                                }
                            }
                        }
                    }
                }
                function tapKey(event, pressed) {
                    if (root.audio.tapOn && root.audio.tapKey(event.key, pressed, event.isAutoRepeat,
                                                              (event.modifiers & Qt.ShiftModifier) !== 0))
                        event.accepted = true
                    return event.accepted
                }
                Keys.onShortcutOverride: event => {
                    if (root.audio.tapOn && [Qt.Key_Space, Qt.Key_Backspace, Qt.Key_Escape].includes(event.key)) {
                        event.accepted = true
                        return
                    }
                    event.accepted = root.audio.loaded && root.hotkeys.actionFor(4, event.key, event.modifiers) !== ""
                }
                Keys.onReleased: event => audioPanel.tapKey(event, false)
                onActiveFocusChanged: if (!activeFocus) root.audio.tapFocusLost()
                Keys.onPressed: event => {
                    if (!root.audio.loaded || audioPanel.tapKey(event, true))
                        return
                    const action = root.hotkeys.actionFor(4, event.key, event.modifiers)
                    if (action === "")
                        return
                    root.runAudioHotkey(action)
                    event.accepted = true
                }
                Connections {
                    target: root.audio
                    function onFocusRequested() { audioDisplay.forceActiveFocus() }
                }
                Connections {
                    target: root.audio
                    function onOpened(path, created) {
                        if (created)
                            root.showPanel(audioDock)
                    }
                }
            }
        }

        PanelDock {
            id: originalDock
            objectName: "originalDock"
            uniqueName: root.engineName("Original")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Translation", "Original text")
            Panel {
                id: originalPanel
                anchors.fill: parent
                objectName: "originalPanel"
                dockName: root.engineName("Original")
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Translation", "Original text: %1").arg(shell.editingTitle)
                                              : qsTranslate("HikariSub.Translation", "Original text")
                accessibleName: qsTranslate("HikariSub.Translation", "Original text")
                minimumBodySize: Qt.size(160, lineText.Layout.minimumHeight)
                Keys.onShortcutOverride: event => event.accepted = editorPanel.panelAction(event) !== ""
                Keys.onPressed: event => {
                    const action = editorPanel.panelAction(event)
                    if (action === "")
                        return
                    root.runEditorHotkey(action, lineText)
                    event.accepted = true
                }
                Item {
                    id: originalSlot
                    objectName: "originalSlot"
                    anchors.fill: parent
                }
                OriginalTextTools {
                    id: originalTools
                    app: root.app
                    editor: root.editor
                    shellRoot: root
                    original: lineText
                    translated: translationText
                }
            }
        }

        PanelDock {
            id: editorDock
            objectName: "editorDock"
            uniqueName: root.engineName("Editor")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Editor", "Line editor")
            Panel {
                id: editorPanel
                anchors.fill: parent
                objectName: "editorPanel"
                dockName: root.engineName("Editor")
                minimumBodySize: Qt.size(Math.max(editorStrip.minimumWidth, editorInspector.minimumWidth)
                                         + 2 * (editorColumn.x - 4),
                                         editorStrip.implicitHeight
                                         + (reviewStrip.visible ? editorColumn.spacing + reviewStrip.implicitHeight : 0)
                                         + (lineTextSlot.visible ? editorColumn.spacing + lineText.Layout.minimumHeight : 0)
                                         + (translationText.visible ? editorColumn.spacing
                                            + translationText.Layout.minimumHeight : 0))
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Editor", "Line editor: %1").arg(shell.editingTitle)
                                              : qsTranslate("HikariSub.Editor", "Line editor")
                function panelAction(event) {
                    if (lineText.activeFocus || translationText.activeFocus)
                        return ""
                    const action = root.hotkeys.actionFor(2, event.key, event.modifiers, false)
                    return action === "EDITBOX_COMMIT" || action === "EDITBOX_COMMIT_GO_NEXT_LINE" ? "" : action
                }
                Keys.onShortcutOverride: event => event.accepted = panelAction(event) !== ""
                Keys.onPressed: event => {
                    const action = panelAction(event)
                    if (action === "")
                        return
                    root.runEditorHotkey(action, root.editedField())
                    event.accepted = true
                }
                Flickable {
                    id: editorScroll
                    objectName: "editorScroll"
                    anchors {
                        fill: parent
                        margins: -4
                    }
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    acceptedButtons: Qt.NoButton
                    readonly property int barWidth: 6
                    readonly property bool overflows: editorColumn.implicitHeight + 8 > height + 0.5
                    contentWidth: width - (overflows ? barWidth : 0)
                    contentHeight: editorColumn.height + 8
                    readonly property Item focusItem: Window.activeFocusItem
                    onFocusItemChanged: Qt.callLater(revealFocus)
                    function inEditor(item) {
                        for (let p = item; p; p = p.parent) {
                            if (p === editorColumn)
                                return true
                        }
                        return false
                    }
                    function reveal(item, rect) {
                        const r = item.mapToItem(contentItem, rect)
                        const room = 1
                        let y = contentY
                        if (r.y + r.height + room > y + height)
                            y = r.y + r.height + room - height
                        if (r.y - room < y)
                            y = r.y - room
                        contentY = Math.max(0, Math.min(y, contentHeight - height))
                    }
                    function revealFocus() {
                        let item = focusItem
                        if (!item || !inEditor(item))
                            return
                        for (let p = item.parent; p && p !== editorColumn; p = p.parent) {
                            if (p.activeFocus && p.height <= height)
                                item = p
                        }
                        if ((item === lineText || item === translationText) && item.height + 6 > height)
                            reveal(item, item.cursorRectangle)
                        else
                            reveal(item, Qt.rect(0, 0, item.width, item.height))
                    }
                    Connections {
                        target: lineText
                        function onCursorRectangleChanged() { if (lineText.activeFocus) Qt.callLater(editorScroll.revealFocus) }
                    }
                    Connections {
                        target: translationText
                        function onCursorRectangleChanged() { if (translationText.activeFocus) Qt.callLater(editorScroll.revealFocus) }
                    }
                    ScrollBar.vertical: ScrollBar {
                        objectName: "editorScrollBar"
                        parent: editorScroll
                        x: editorScroll.width - width - 2
                        y: 2
                        height: editorScroll.height - 4
                        width: editorScroll.barWidth
                        padding: 0
                        policy: editorScroll.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        focusPolicy: Qt.NoFocus
                        background: Rectangle {
                            implicitWidth: editorScroll.barWidth
                            radius: editorScroll.barWidth / 2
                            color: Theme.field
                            border.color: Theme.line
                        }
                        contentItem: Rectangle {
                            implicitWidth: editorScroll.barWidth
                            implicitHeight: 12
                            radius: editorScroll.barWidth / 2
                            color: parent.pressed ? Theme.text : Theme.muted
                            opacity: parent.pressed || parent.hovered ? 0.9 : 0.6
                        }
                    }

                    ColumnLayout {
                        id: editorColumn
                        objectName: "editorContent"
                        x: 12
                        y: 4
                        width: editorScroll.contentWidth - 2 * x
                        spacing: 6
                        height: Math.max(editorScroll.height - 8, implicitHeight)

                        LineEditorStrip {
                            id: editorStrip
                            shellRoot: root
                            editor: root.editor
                            app: root.app
                            tagButtons: root.tagButtons
                            fontPicker: fontPicker
                            original: lineText
                            translated: translationText
                            translationTools: root.editor.translationMode && !root.originalShown
                            aiShown: root.inlineAi.on && root.editor.hasLine
                            onAiActionsRequested: anchor => editorAiMenu.popup(anchor, 0, anchor.height)
                            Layout.fillWidth: true
                            Layout.minimumWidth: minimumWidth
                        }
                        InlineAiMenu {
                            id: editorAiMenu
                            objectName: "editorAiMenu"
                            controller: root.inlineAi
                            editor: true
                            onChosen: ask => inlineAiBox.begin(ask, true, lineText)
                            onRepeatRequested: root.repeatInlineAi(true, lineText)
                            onSettingsRequested: root.openSettings("ai")
                        }

                        ReviewStrip {
                            id: reviewStrip
                            review: root.review
                            Layout.fillWidth: true
                            onUndoRequested: root.undoAiLine()
                            onKeepRequested: root.review.keepLine()
                        }

                        Label {
                            objectName: "originalCaption"
                            visible: lineTextSlot.visible && root.editor.translationMode
                            text: qsTranslate("HikariSub.Editor", "Original text")
                            color: Theme.muted
                            Layout.bottomMargin: -4
                        }

                        Item {
                            id: lineTextSlot
                            objectName: "lineTextSlot"
                            visible: !root.originalShown
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: lineText.Layout.minimumHeight
                            implicitWidth: lineText.implicitWidth
                            implicitHeight: lineText.implicitHeight
                            RoleField {
                                id: lineText
                                objectName: "lineText"
                                parent: root.originalShown ? originalSlot : lineTextSlot
                                anchors.fill: parent
                                role: 0
                                focus: true
                                tabNext: root.originalShown ? translationText : null
                                Accessible.name: root.editor.translationMode ? qsTranslate("HikariSub.Editor", "Original text") : qsTranslate("HikariSub.Editor", "Line text")
                            }
                        }
                        Label {
                            objectName: "translatedCaption"
                            visible: lineTextSlot.visible && root.editor.translationMode
                            text: qsTranslate("HikariSub.Editor", "Translated text")
                            color: Theme.muted
                            Layout.bottomMargin: -4
                        }
                        RoleField {
                            id: translationText
                            objectName: "translationText"
                            role: 1
                            visible: root.editor.translationMode
                            tabPrevious: root.originalShown ? lineText : null
                            Accessible.name: qsTranslate("HikariSub.Editor", "Translated text")
                        }
                        RunStrip {
                            editor: root.editor
                            shellRoot: root
                            translated: translationText
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "karaokeCarryNotice"
                            visible: root.editor.translationMode && root.editor.karaokeCarried
                            text: qsTranslate("HikariSub.Translation", "Karaoke line: the tags before its text are carried, the karaoke timing is not.")
                            color: Theme.muted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            objectName: "editorMessages"
                            visible: root.editor.problem.length > 0 || root.editor.attempted.length > 0
                            spacing: 6
                            Layout.fillWidth: true
                            Icon {
                                iconRole: "notice-error"
                                size: 12
                                color: Theme.danger
                                accentColor: Theme.danger
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 3
                            }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Label {
                                    id: editorProblem
                                    objectName: "editorProblem"
                                    visible: text.length > 0
                                    text: root.editor.problem
                                    color: Theme.danger
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                    Accessible.role: Accessible.AlertMessage
                                }
                                Label {
                                    id: editorAttempted
                                    objectName: "editorAttempted"
                                    visible: root.editor.attempted.length > 0
                                    text: qsTranslate("HikariSub.Editor", "Not applied: %1").arg(root.editor.attempted)
                                    color: Theme.danger
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        LineInspector {
                            id: editorInspector
                            objectName: "lineInspector"
                            editor: root.editor
                            hotkeys: root.hotkeys
                            Layout.fillWidth: true
                            onStyleEditRequested: style => root.lazy(styleManagerWindowLoader).editStyle(style)
                        }
                    }
                }
            }
        }

        PanelDock {
            id: gridDock
            objectName: "gridDock"
            uniqueName: root.engineName("Grid")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Grid", "Grid")
            Panel {
                id: gridPanel
                anchors.fill: parent
                objectName: "gridPanel"
                dockName: root.engineName("Grid")
                minimumBodySize: Qt.size(240, grid.rowHeight * 3)
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Grid", "Editing: %1").arg(shell.editingTitle) : qsTranslate("HikariSub.Grid", "No document open")
                accessibleName: qsTranslate("HikariSub.Grid", "Grid")
                focus: true
                keyTarget: grid
                InlineAiBar {
                    id: inlineAiBar
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    z: 1
                    controller: root.inlineAi
                    onSettingsRequested: root.openSettings("ai")
                }
                HikariGrid {
                    id: grid
                    objectName: "editingGrid"
                    anchors.fill: parent
                    anchors.topMargin: inlineAiBar.visible ? inlineAiBar.height : 0
                    focus: true
                    // Qt Quick puts only items with an Accessible attachment in the accessibility tree.
                    Accessible.role: Accessible.Table
                    model: shell.lines
                    columnWidths: root.shell.gridColumnWidths
                    onColumnWidthsEdited: root.shell.gridColumnWidths = columnWidths
                    onContentYChanged: root.app.setTargetScroll(Math.floor(contentY / rowHeight))
                    onActiveLineRequested: id => root.app.selectLine(id)
                    onActiveLineFallbackRequested: id => root.app.moveActiveLine(id)
                    onExtendRequested: rows => root.app.extendSelection(rows)
                    onLineClicked: (id, modifiers, endColumn, doubleClick) => root.app.clickLine(id, modifiers, endColumn, doubleClick)
                    onLineDragged: id => root.app.dragSelection(id)
                    onSelectAllRequested: root.app.selectAllLines()
                    property bool aiMenuNext: false
                    onContextMenuRequested: (x, y) => {
                        if (aiMenuNext) {
                            aiMenuNext = false
                            gridAiHotkeyMenu.popup(grid, x, y)
                        } else {
                            gridMenu.popup(grid, x, y)
                        }
                    }
                    InlineAiMenu {
                        id: gridAiHotkeyMenu
                        objectName: "gridAiHotkeyMenu"
                        controller: root.inlineAi
                        lineCount: Math.max(1, root.shell.selectedCount)
                        onChosen: ask => inlineAiBox.begin(ask, false, null)
                        onRepeatRequested: root.repeatInlineAi(false, null)
                        onSettingsRequested: root.openSettings("ai")
                    }
                    onHeaderMenuRequested: (x, y) => gridHeaderMenu.popup(grid, x, y)
                    MenuKey.onRequested: grid.requestMenu()
                    GridToolTip { grid: grid }
                    GridScrollBars { grid: grid }
                    onHiddenBlockToggleRequested: row => root.app.toggleHiddenBlock(row)
                    onGroupToggleRequested: id => root.app.toggleGroup(id)
                    onFoldToggleRequested: id => root.app.toggleFold(id)
                    onGroupMenuRequested: (id, x, y) => {
                        groupMenu.description = id
                        groupMenu.at = Qt.point(x, y)
                        groupMenu.popup(grid, x, y)
                    }
                    ShellMenu {
                        id: groupMenu
                        objectName: "groupMenu"
                        property var description: 0
                        property point at: Qt.point(0, 0)
                        ShellMenuItem { iconRole: "tree-select"; objectName: "groupSelect"; text: qsTranslate("HikariSub.Grid", "Select the group’s lines"); onTriggered: root.app.selectGroup(groupMenu.description) }
                        ShellMenuItem {
                            iconRole: "tree-add-lines"
                            objectName: "groupAddLines"
                            text: qsTranslate("HikariSub.Grid", "Add %n selected line(s)", "", Math.max(1, root.shell.selectedCount))
                            onTriggered: root.app.addLinesToGroup(groupMenu.description)
                        }
                        ShellMenuItem { iconRole: "edit-copy"; objectName: "groupCopy"; text: qsTranslate("HikariSub.Grid", "Copy line group"); onTriggered: root.app.copyGroup(groupMenu.description) }
                        ShellMenuItem {
                            iconRole: "edit"
                            objectName: "groupRename"; text: qsTranslate("HikariSub.Grid", "Change description…")
                            onTriggered: groupDescriptionPopover.edit(groupMenu.description, grid, groupMenu.at)
                        }
                        ShellMenuItem {
                            command: "GLOBAL_CONVERT_FOLDS_TO_GROUPS"
                            iconRole: "convert-folds"
                            objectName: "groupConvertFolds"
                            text: qsTranslate("HikariSub.Grid", "Convert Aegisub folds to line groups")
                            enabled: groupMenu.visible && root.app.canConvertFolds()
                            onTriggered: if (!root.hotkeyGesture("GLOBAL_CONVERT_FOLDS_TO_GROUPS")) root.app.convertFoldsToGroups()
                        }
                        MenuSeparator {}
                        ShellMenuItem { iconRole: "remove"; objectName: "groupDelete"; text: qsTranslate("HikariSub.Grid", "Ungroup"); onTriggered: root.app.removeGroup(groupMenu.description) }
                    }
                    function clipboardKey(event) {
                        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
                        return mods === Qt.ControlModifier && (event.key === Qt.Key_C || event.key === Qt.Key_X || event.key === Qt.Key_V)
                    }
                    Keys.onShortcutOverride: event => {
                        event.accepted = clipboardKey(event) || root.hotkeys.actionFor(1, event.key, event.modifiers) !== ""
                                         || grid.foldsWith(event.key, event.modifiers)
                    }
                    Keys.onPressed: event => {
                        if (clipboardKey(event)) {
                            if (event.key === Qt.Key_C)
                                root.app.copyLines()
                            else if (event.key === Qt.Key_X)
                                root.app.cutLines()
                            else
                                root.app.pasteLines()
                            event.accepted = true
                            return
                        }
                        const action = root.hotkeys.actionFor(1, event.key, event.modifiers)
                        if (action === "")
                            return
                        root.runGridHotkey(action)
                        event.accepted = true
                    }
                    ShellMenu {
                        id: gridMenu
                        objectName: "gridMenu"
                        property bool canPasteTranslation: false
                        property bool canShiftTranslation: false
                        readonly property int lineCount: Math.max(1, root.shell.selectedCount)
                        property bool reviewShown: false
                        onAboutToShow: {
                            canPasteTranslation = root.app.canPasteTranslation()
                            canShiftTranslation = root.app.canShiftTranslation()
                            reviewShown = root.review.on && root.review.current.count !== undefined
                        }
                        Instantiator {
                            model: gridMenu.reviewShown ? [
                                { command: "GLOBAL_KEEP_AI_CHANGE", icon: "review-keep", action: keepAiChangeAction },
                                { command: "GLOBAL_UNDO_AI_CHANGE", icon: "undo", action: undoAiChangeAction },
                                { command: "GLOBAL_NEXT_AI_CHANGE", icon: "chevron-down", action: nextAiChangeAction },
                                { command: "GLOBAL_SHOW_AI_REVIEW", icon: "show", action: showAiReviewAction,
                                  text: qsTranslate("HikariSub.Review", "Show in Review panel") }
                            ] : []
                            delegate: ShellMenuItem {
                                required property var modelData
                                objectName: "gridReview_" + modelData.command
                                command: modelData.command
                                iconRole: modelData.icon
                                action: modelData.action
                                text: modelData.text ?? modelData.action.text
                            }
                            onObjectAdded: (index, object) => gridMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => gridMenu.removeItem(object)
                        }
                        MenuSeparator {
                            objectName: "gridReviewSeparator"
                            visible: gridMenu.reviewShown
                            height: visible ? implicitHeight : 0
                        }
                        ShellMenu {
                            objectName: "gridInsertMenu"
                            iconRole: "add"
                            title: qsTranslate("HikariSub.Grid", "Insert")
                            ShellMenuItem { command: "GRID_INSERT_BEFORE"; iconRole: "insert-before"; objectName: "insertBefore"; text: qsTranslate("HikariSub.Grid", "Before"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE")) root.app.insertLine(true) }
                            ShellMenuItem { command: "GRID_INSERT_AFTER"; iconRole: "insert-after"; objectName: "insertAfter"; text: qsTranslate("HikariSub.Grid", "After"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER")) root.app.insertLine(false) }
                            MenuSeparator {}
                            ShellMenuItem {
                                command: "GRID_INSERT_BEFORE_VIDEO"
                                iconRole: "insert-before"
                                objectName: "insertBeforeVideo"; text: qsTranslate("HikariSub.Grid", "Before, at video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_VIDEO")) root.app.insertLine(true, "video")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_AFTER_VIDEO"
                                iconRole: "insert-after"
                                objectName: "insertAfterVideo"; text: qsTranslate("HikariSub.Grid", "After, at video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_VIDEO")) root.app.insertLine(false, "video")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME"
                                iconRole: "insert-before"
                                objectName: "insertBeforeFrame"; text: qsTranslate("HikariSub.Grid", "Before, at video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME")) root.app.insertLine(true, "frame")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_AFTER_WITH_VIDEO_FRAME"
                                iconRole: "insert-after"
                                objectName: "insertAfterFrame"; text: qsTranslate("HikariSub.Grid", "After, at video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_WITH_VIDEO_FRAME")) root.app.insertLine(false, "frame")
                            }
                        }
                        ShellMenuItem {
                            command: "GRID_DUPLICATE_LINES"
                            objectName: "duplicateLines"
                            iconRole: "duplicate"
                            text: qsTranslate("HikariSub.Grid", "Duplicate %n line(s)", "", gridMenu.lineCount)
                            readonly property string docsText: qsTranslate("HikariSub.Hotkeys", "Duplicate lines")
                            onTriggered: if (!root.gridGesture(this, "GRID_DUPLICATE_LINES")) root.app.duplicateLines()
                        }
                        ShellMenuItem {
                            command: "GLOBAL_BLUR_AND_GLOW_LAYERS"
                            objectName: "gridBlurAndGlowLayers"
                            iconRole: "layers"
                            text: qsTranslate("HikariSub.Hotkeys", "Blur and glow layers…")
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_BLUR_AND_GLOW_LAYERS")) root.openBlurAndGlowLayers()
                        }
                        ShellMenuItem {
                            command: "GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT"
                            objectName: "gridBlurAndGlowLayersRepeat"
                            iconRole: "layers-repeat"
                            text: qsTranslate("HikariSub.Hotkeys", "Build layers with the last values")
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT")) root.repeatBlurAndGlowLayers()
                        }
                        ShellMenuItem { command: "GRID_CUT"; iconRole: "edit-cut"; objectName: "cutLines"; standardKey: StandardKey.Cut; text: qsTranslate("HikariSub.Grid", "Cut"); onTriggered: if (!root.gridGesture(this, "GRID_CUT")) root.app.cutLines() }
                        ShellMenuItem { command: "GRID_COPY"; iconRole: "edit-copy"; objectName: "copyLines"; standardKey: StandardKey.Copy; text: qsTranslate("HikariSub.Grid", "Copy"); onTriggered: if (!root.gridGesture(this, "GRID_COPY")) root.app.copyLines() }
                        ShellMenuItem { command: "GRID_PASTE"; iconRole: "edit-paste"; objectName: "pasteLines"; standardKey: StandardKey.Paste; text: qsTranslate("HikariSub.Grid", "Paste"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE")) root.app.pasteLines() }
                        ShellMenu {
                            objectName: "gridPasteSpecialMenu"
                            iconRole: "paste-special"
                            title: qsTranslate("HikariSub.Grid", "Paste special")
                            ShellMenuItem { command: "GRID_COPY_COLUMNS"; iconRole: "columns-copy"; objectName: "copyColumns"; text: qsTranslate("HikariSub.Grid", "Copy columns"); onTriggered: if (!root.gridGesture(this, "GRID_COPY_COLUMNS")) root.chooseColumns(false) }
                            ShellMenuItem { command: "GRID_PASTE_COLUMNS"; iconRole: "columns-paste"; objectName: "pasteColumns"; text: qsTranslate("HikariSub.Grid", "Paste columns"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE_COLUMNS")) root.chooseColumns(true) }
                            MenuSeparator {}
                            ShellMenuItem {
                                command: "GRID_PASTE_TRANSLATION"
                                iconRole: "paste-translation"
                                objectName: "pasteTranslation"
                                text: qsTranslate("HikariSub.Grid", "Paste translation from a file…")
                                enabled: gridMenu.canPasteTranslation
                                help: root.app.translationHint
                                onTriggered: {
                                    if (root.gridGesture(this, "GRID_PASTE_TRANSLATION"))
                                        return
                                    translationFileDialog.currentFolder = root.app.targetFolder()
                                    translationFileDialog.open()
                                }
                            }
                        }
                        InlineAiMenu {
                            objectName: "gridAiMenu"
                            controller: root.inlineAi
                            lineCount: gridMenu.lineCount
                            onChosen: ask => inlineAiBox.begin(ask, false, null)
                            onRepeatRequested: root.repeatInlineAi(false, null)
                            onSettingsRequested: root.openSettings("ai")
                        }
                        MenuSeparator {}
                        ShellMenu {
                            objectName: "gridJoinMenu"
                            iconRole: "join-lines"
                            title: qsTranslate("HikariSub.Grid", "Join")
                            ShellMenuItem { command: "GRID_JOIN_LINES"; iconRole: "join-lines"; objectName: "joinLines"; text: qsTranslate("HikariSub.Grid", "Join"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_LINES")) root.app.joinLines("join") }
                            ShellMenuItem { command: "GRID_JOIN_TO_FIRST_LINE"; iconRole: "join-keep-first"; objectName: "joinFirst"; text: qsTranslate("HikariSub.Grid", "Keep first"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_FIRST_LINE")) root.app.joinLines("first") }
                            ShellMenuItem { command: "GRID_JOIN_TO_LAST_LINE"; iconRole: "join-keep-last"; objectName: "joinLast"; text: qsTranslate("HikariSub.Grid", "Keep last"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_LAST_LINE")) root.app.joinLines("last") }
                        }
                        ShellMenu {
                            objectName: "gridSplitMenu"
                            iconRole: "split-lines"
                            title: qsTranslate("HikariSub.Grid", "Split")
                            onAboutToShow: bakeAnimationsItem.checked = root.app.settings.value("grid.splitFramesBakeAnimations")
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_VIDEO_TIME"
                                iconRole: "split-at-video"
                                objectName: "splitAtVideoTime"
                                text: qsTranslate("HikariSub.Grid", "At video time")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_VIDEO_TIME")) root.app.splitLines("videoTime")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_FRAME"
                                iconRole: "split-frames"
                                objectName: "splitIntoFrames"
                                text: qsTranslate("HikariSub.Grid", "Into frames")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_FRAME")) root.app.splitLines("frames")
                            }
                            ShellMenuItem {
                                id: bakeAnimationsItem
                                objectName: "splitFramesBakeAnimations"
                                text: qsTranslate("HikariSub.Grid", "Bake animations")
                                checkable: true
                                onTriggered: root.app.settings.setValue("grid.splitFramesBakeAnimations", checked)
                            }
                            MenuSeparator {}
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_CHARS"
                                iconRole: "split-characters"
                                objectName: "splitIntoCharacters"
                                text: qsTranslate("HikariSub.Grid", "Into characters")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_CHARS")) root.app.splitLines("chars")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_WORDS"
                                iconRole: "split-words"
                                objectName: "splitIntoWords"
                                text: qsTranslate("HikariSub.Grid", "Into words")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WORDS")) root.app.splitLines("words")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_WRAPS"
                                iconRole: "split-wraps"
                                objectName: "splitByWraps"
                                text: qsTranslate("HikariSub.Grid", "By wraps")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WRAPS")) root.app.splitLines("wraps")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_AT_TAG_BLOCKS"
                                iconRole: "split-tag-blocks"
                                objectName: "splitAtTagBlocks"
                                text: qsTranslate("HikariSub.Grid", "At tag blocks")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_AT_TAG_BLOCKS")) root.app.splitLines("tagBlocks")
                            }
                            MenuSeparator {}
                            ShellMenuItem {
                                command: "GRID_SPLIT_EVERY_N"
                                iconRole: "split-every-n"
                                objectName: "splitEveryN"
                                text: qsTranslate("HikariSub.Grid", "Every N frames or seconds…")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_EVERY_N")) root.openSplitEvery()
                            }
                        }
                        ShellMenuItem { command: "GLOBAL_REMOVE_TAGS"; iconRole: "remove-tags"; objectName: "gridRemoveTags"; action: removeTagsAction }
                        ShellMenuItem {
                            command: "GLOBAL_INTERPOLATE_LINES"
                            iconRole: "interpolate-lines"
                            objectName: "interpolateLines"
                            text: qsTranslate("HikariSub.Grid", "Interpolate across lines…")
                            help: root.shell.selectedCount < 3
                                  ? qsTranslate("HikariSub.Grid", "Select at least three lines: the first, the last and the lines between them.")
                                  : qsTranslate("HikariSub.Grid", "Fills the lines between the first and last selected line with the tag values between theirs")
                            enabled: root.shell.hasEditingTarget && root.shell.selectedCount >= 3
                            onTriggered: if (!root.hotkeyGesture("GLOBAL_INTERPOLATE_LINES")) root.openInterpolateLines()
                        }
                        ShellMenuItem { command: "GRID_SWAP_LINES"; iconRole: "swap-lines"; objectName: "swapLines"; text: qsTranslate("HikariSub.Grid", "Swap lines"); onTriggered: if (!root.gridGesture(this, "GRID_SWAP_LINES")) root.app.swapLines() }
                        ShellMenu {
                            objectName: "gridContinuousMenu"
                            iconRole: "continuous-next"
                            title: qsTranslate("HikariSub.Grid", "Make times continuous")
                            ShellMenuItem {
                                command: "GRID_MAKE_CONTINOUS_PREVIOUS_LINE"
                                iconRole: "continuous-previous"
                                objectName: "continuousPrevious"; text: qsTranslate("HikariSub.Grid", "With the previous line")
                                onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_PREVIOUS_LINE")) root.app.makeContinuous(true)
                            }
                            ShellMenuItem {
                                command: "GRID_MAKE_CONTINOUS_NEXT_LINE"
                                iconRole: "continuous-next"
                                objectName: "continuousNext"; text: qsTranslate("HikariSub.Grid", "With the next line")
                                onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_NEXT_LINE")) root.app.makeContinuous(false)
                            }
                        }
                        ShellMenuItem { command: "GRID_TREE_MAKE"; iconRole: "make-tree"; objectName: "makeTree"; text: qsTranslate("HikariSub.Grid", "Make line group"); onTriggered: if (!root.gridGesture(this, "GRID_TREE_MAKE")) root.app.makeGroups() }
                        ShellMenuItem {
                            command: "GLOBAL_CONVERT_FOLDS_TO_GROUPS"
                            iconRole: "convert-folds"
                            objectName: "convertFolds"
                            text: qsTranslate("HikariSub.Grid", "Convert Aegisub folds to line groups")
                            enabled: gridMenu.visible && root.app.canConvertFolds()
                            onTriggered: if (!root.hotkeyGesture("GLOBAL_CONVERT_FOLDS_TO_GROUPS")) root.app.convertFoldsToGroups()
                        }
                        MenuSeparator {}
                        LineBreaksMenu { shellRoot: root; prefix: "grid" }
                        ChangeCaseMenu { shellRoot: root; prefix: "grid" }
                        ShellMenuItem {
                            command: "GLOBAL_SWAP_DIALOGUE_ALTERNATIVES"
                            iconRole: "dialogue-swap"
                            objectName: "gridSwapAlternatives"
                            text: qsTranslate("HikariSub.Text", "Swap dialogue alternatives")
                            onTriggered: if (!root.hotkeyGesture(command)) root.app.textCommand("swapAlternatives")
                        }
                        MenuSeparator {}
                        ShellMenuItem {
                            command: "GRID_SELECT_VISIBLE_LINES"
                            iconRole: "select-visible"
                            objectName: "selectVisibleLines"; text: qsTranslate("HikariSub.Grid", "Select lines visible on video")
                            onTriggered: if (!root.gridGesture(this, "GRID_SELECT_VISIBLE_LINES")) root.app.selectLinesVisibleOnVideo()
                        }
                        ShellMenuItem {
                            command: "GRID_SHOW_PREVIEW"
                            iconRole: "reference"
                            objectName: "showPreview"
                            text: qsTranslate("HikariSub.Grid", "Show subtitles preview")
                            enabled: gridMenu.visible && root.app.canShowPreview()
                            onTriggered: if (!root.gridGesture(this, "GRID_SHOW_PREVIEW")) root.app.showPreview()
                        }
                        ShellMenuItem {
                            command: "GRID_TRANSLATION_DIALOG"
                            iconRole: "translation-shift"
                            objectName: "translationDialog"
                            text: qsTranslate("HikariSub.Grid", "Translation matching…")
                            enabled: gridMenu.canShiftTranslation
                            onTriggered: if (!root.gridGesture(this, "GRID_TRANSLATION_DIALOG")) root.lazy(translationShiftWindowLoader).open()
                        }
                        MenuSeparator {}
                        ShellMenuItem { command: "GLOBAL_REMOVE_TEXT"; iconRole: "clear"; objectName: "deleteText"; text: qsTranslate("HikariSub.Grid", "Delete text"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_TEXT")) root.app.deleteText() }
                        ShellMenuItem {
                            command: "GLOBAL_REMOVE_LINES"
                            iconRole: "delete"
                            objectName: "deleteLines"
                            text: qsTranslate("HikariSub.Grid", "Delete %n line(s)", "", gridMenu.lineCount)
                            readonly property string docsText: qsTranslate("HikariSub.Hotkeys", "Delete line")
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_LINES")) root.app.deleteLines()
                        }
                        MenuSeparator {}
                        ShellMenuItem {
                            command: "GLOBAL_ADD_QC_NOTE"
                            iconRole: "qc-notes"
                            objectName: "addQcNote"
                            text: qsTranslate("HikariSub.QcNotes", "Add QC note…")
                            enabled: root.qcNotes.editable
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_ADD_QC_NOTE")) root.addQcNote(root.qcNotes.lineStart())
                        }
                    }
                    GridColumnsMenu {
                        id: gridHeaderMenu
                        objectName: "gridHeaderMenu"
                        prefix: "header"
                    }
                }
                Label {
                    objectName: "gridEmptyState"
                    anchors.centerIn: parent
                    visible: !shell.hasEditingTarget
                    text: qsTranslate("HikariSub.Grid", "No document open")
                    color: gridPanel.palette.placeholderText
                    Accessible.ignored: true
                }
            }
        }

        PanelDock {
            id: referenceDock
            objectName: "referenceDock"
            uniqueName: root.engineName("Reference")
            affinities: root.dockAffinities
            title: shell.hasReference ? qsTranslate("HikariSub.Reference", "Reference: %1").arg(shell.referenceTitle) : qsTranslate("HikariSub.Reference", "Reference")
            Panel {
                id: referencePanel
                anchors.fill: parent
                objectName: "referencePanel"
                dockName: root.engineName("Reference")
                keyTarget: referenceTray.grid
                minimumBodySize: Qt.size(240, referenceTray.grid.rowHeight * 3)
                visible: shell.hasReference
                title: qsTranslate("HikariSub.Reference", "Reference (protected, read-only): %1").arg(shell.referenceTitle)
                ReferenceTray {
                    id: referenceTray
                    anchors.fill: parent
                    focus: true
                    app: root.app
                    shell: root.shell
                    hotkeys: root.hotkeys
                    shellRoot: root
                    editingGrid: grid
                }
            }
        }

        PanelDock {
            id: timingDock
            objectName: "timingDock"
            uniqueName: root.engineName("Timing")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.TimeShift", "Shift times")
            Panel {
                id: timingPanel
                objectName: "timingPanel"
                dockName: root.engineName("Timing")
                minimumBodySize: shiftTimesPanel.minimumBodySize
                anchors.fill: parent
                title: qsTranslate("HikariSub.TimeShift", "Shift times")
                ShiftTimesPanel {
                    id: shiftTimesPanel
                    objectName: "shiftTimesPanel"
                    anchors.fill: parent
                    focus: true
                    shiftTimes: root.shiftTimes
                    app: root.app
                    shellWindow: root.shellWindow
                    messages: root.messages
                    hasVideo: root.video.hasVideo
                    audioHasMark: root.audio.hasMark
                    onApply: root.applyShift("shift")
                    onEscaped: root.focusPanel(gridPanel, Qt.OtherFocusReason)
                    onOpenVideo: openVideoAction.trigger()
                }
            }
        }

        PanelDock {
            id: postprocessorDock
            objectName: "postprocessorDock"
            uniqueName: root.engineName("Postprocessor")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.TimeShift", "Timing postprocessor")
            Panel {
                id: postprocessorPanel
                objectName: "postprocessorPanel"
                dockName: root.engineName("Postprocessor")
                minimumBodySize: postprocessorTool.minimumBodySize
                anchors.fill: parent
                title: qsTranslate("HikariSub.TimeShift", "Timing postprocessor")
                TimingPostprocessorPanel {
                    id: postprocessorTool
                    anchors.fill: parent
                    focus: true
                    shiftTimes: root.shiftTimes
                    app: root.app
                    shellWindow: root.shellWindow
                    messages: root.messages
                    onApply: root.applyShift("postprocessor")
                    onEscaped: root.focusPanel(gridPanel, Qt.OtherFocusReason)
                    onOpenVideo: openVideoAction.trigger()
                    onOpenKeyframes: openKeyframesAction.trigger()
                }
                Connections {
                    target: root.editor
                    function onChanged() { shiftTimesPanel.schedule(); postprocessorTool.schedule() }
                }
                Connections {
                    target: root.video
                    function onChanged() { shiftTimesPanel.schedule(); postprocessorTool.schedule() }
                    function onTextsChanged() { shiftTimesPanel.schedule(); postprocessorTool.schedule() }
                }
                Connections {
                    target: root.audio
                    function onMarkChanged() { shiftTimesPanel.schedule(); postprocessorTool.schedule() }
                }
            }
        }

        PanelDock {
            id: qcNotesDock
            objectName: "qcNotesDock"
            uniqueName: root.engineName("QcNotes")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.QcNotes", "QC notes")
            Panel {
                id: qcNotesPanel
                objectName: "qcNotesPanel"
                dockName: root.engineName("QcNotes")
                minimumBodySize: qcNotesTool.minimumBodySize
                anchors.fill: parent
                title: qsTranslate("HikariSub.QcNotes", "QC notes")
                QcNotesPanel {
                    id: qcNotesTool
                    anchors.fill: parent
                    focus: true
                    qcNotes: root.qcNotes
                    messages: root.messages
                    onEscaped: root.focusPanel(gridPanel, Qt.OtherFocusReason)
                }
            }
        }

        PanelDock {
            id: reviewDock
            objectName: "reviewDock"
            uniqueName: root.engineName("Review")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Review", "Review")
            Panel {
                id: reviewPanel
                objectName: "reviewPanel"
                dockName: root.engineName("Review")
                minimumBodySize: Qt.size(240, 160)
                anchors.fill: parent
                title: qsTranslate("HikariSub.Review", "Review")
                Loader {
                    id: reviewLoader
                    anchors.fill: parent
                    active: root.review.on
                    focus: true
                    sourceComponent: ReviewPanel {
                        review: root.review
                        targetTitle: root.shell.hasEditingTarget ? root.shell.editingTitle : ""
                        onGoToTab: document => {
                            const tabs = root.app.windowTabs(root.shellWindow)
                            const index = tabs.findIndex(t => t.id === document)
                            if (index >= 0)
                                root.app.selectTab(index)
                        }
                        onRestoreRequested: turn => restoreCheckpointDialog.openFor(turn)
                    }
                }
                Connections {
                    target: root.review
                    function onChanged() {
                        if (!root.review.on && reviewDock.isOpen)
                            reviewDock.close()
                    }
                }
            }
        }

        PanelDock {
            id: agentDock
            objectName: "agentDock"
            uniqueName: root.engineName("Agent")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Agent", "Agent")
            Panel {
                id: agentPanel
                objectName: "agentPanel"
                dockName: root.engineName("Agent")
                minimumBodySize: Qt.size(300, 240)
                anchors.fill: parent
                title: qsTranslate("HikariSub.Agent", "Agent")
                Loader {
                    id: agentLoader
                    anchors.fill: parent
                    active: root.agent.on
                    focus: true
                    sourceComponent: AgentPanel {
                        agent: root.agent
                        onReviewRequested: root.showPanel(reviewDock)
                        onRestoreRequested: turn => restoreCheckpointDialog.openFor(turn)
                        onSettingsRequested: root.openSettings("ai")
                        onLogRequested: root.showPanel(logDock)
                        onExportRequested: id => agentExportDialog.openFor(id)
                    }
                }
                Connections {
                    target: root.agent
                    function onSwitched() {
                        if (!root.agent.on && agentDock.isOpen)
                            agentDock.close()
                    }
                }
            }
        }

        PanelDock {
            id: searchDock
            objectName: "searchDock"
            uniqueName: root.engineName("Search")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Search", "Find")
            onIsOpenChanged: if (!isOpen) searchTool.save()
            Panel {
                id: searchPanel
                objectName: "searchPanel"
                dockName: root.engineName("Search")
                minimumBodySize: searchTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.Search", "Find")
                onActiveFocusChanged: if (activeFocus) searchTool.activated()
                SearchTool {
                    id: searchTool
                    anchors.fill: parent
                    app: root.app
                    shellWindow: root.shellWindow
                    editor: root.editor
                    onEscaped: root.focusPanel(editorPanel, Qt.OtherFocusReason)
                }
            }
        }

        PanelDock {
            id: automationDock
            objectName: "automationDock"
            uniqueName: root.engineName("Automation")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Automation", "Automation")
            Panel {
                id: automationPanel
                objectName: "automationPanel"
                dockName: root.engineName("Automation")
                minimumBodySize: automationTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.Automation", "Automation")
                AutomationManager {
                    id: automationTool
                    objectName: "automationTool"
                    anchors.fill: parent
                    controller: root.automationManager
                    automation: root.automation
                    macroHotkeys: root.automationHotkeys
                    hotkeys: root.hotkeys
                    loadAction: loadScriptAction
                    refreshAction: reloadAutoloadAction
                    documentName: root.shell.editingTitle
                    documentTab: root.app.currentTab
                    settings: root.app.settings
                    notices: root.notices
                }
            }
        }

        PanelDock {
            id: spellDock
            objectName: "spellDock"
            uniqueName: root.engineName("Spellchecker")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Spelling", "Spellchecker")
            onIsOpenChanged: if (!isOpen) spellTool.stop()
            Panel {
                id: spellPanel
                objectName: "spellPanel"
                dockName: root.engineName("Spellchecker")
                minimumBodySize: spellTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.Spelling", "Spellchecker")
                readonly property string guideId: spellTool.grammarMode ? "Spellchecker.grammar" : ""
                SpellCheckerPanel {
                    id: spellTool
                    objectName: "spellCheckerPanel"
                    anchors.fill: parent
                    focus: true
                    app: root.app
                    shellWindow: root.shellWindow
                    notices: root.notices
                    documentName: root.shell.editingTitle
                    onSettingsRequested: root.openSettings("spelling")
                }
            }
        }

        PanelDock {
            id: stylesDock
            objectName: "stylesDock"
            uniqueName: root.engineName("Styles")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.Styles", "Styles")
            Panel {
                id: stylesPanel
                objectName: "stylesPanel"
                dockName: root.engineName("Styles")
                minimumBodySize: Qt.size(180, 120)
                anchors.fill: parent
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Styles", "Styles: %1").arg(shell.editingTitle)
                                              : qsTranslate("HikariSub.Styles", "Styles")
                accessibleName: qsTranslate("HikariSub.Styles", "Styles")
                StylesPanel {
                    id: stylesTool
                    objectName: "stylesTool"
                    anchors.fill: parent
                    styles: root.styleManager
                    shellWindow: root.shellWindow
                    editor: root.editor
                    app: root.app
                    managerLoader: styleManagerWindowLoader
                    catalogs: root.fontCatalogs
                    live: stylesDock.isOpen
                }
            }
        }

        PanelDock {
            id: templateDock
            objectName: "templateDock"
            uniqueName: root.engineName("Template")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.TemplateEditor", "Template editor")
            Binding {
                target: root.templateEditor
                property: "shown"
                value: templateDock.isOpen && templatePanel.visible
            }
            Panel {
                id: templatePanel
                objectName: "templatePanel"
                dockName: root.engineName("Template")
                minimumBodySize: templateTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.TemplateEditor", "Template editor")
                TemplateEditor {
                    id: templateTool
                    objectName: "templateTool"
                    anchors.fill: parent
                    controller: root.templateEditor
                    video: root.video
                }
            }
        }

        PanelDock {
            id: nodeGraphDock
            objectName: "nodeGraphDock"
            uniqueName: root.engineName("NodeGraph")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.NodeEffects", "Graph")
            Binding {
                target: root.nodeEffects
                property: "shown"
                value: root.workspaceLayout.nodeEffects || (nodeGraphDock.isOpen && nodeGraphPanel.visible)
            }
            Panel {
                id: nodeGraphPanel
                objectName: "nodeGraphPanelHost"
                dockName: root.engineName("NodeGraph")
                minimumBodySize: nodeGraphTool.minimumSize
                anchors.fill: parent
                title: root.nodeEffects.hasGraph ? qsTranslate("HikariSub.NodeEffects", "Graph: %1").arg(root.nodeEffects.name)
                                                 : qsTranslate("HikariSub.NodeEffects", "Graph")
                accessibleName: qsTranslate("HikariSub.NodeEffects", "Graph")
                NodeGraphCanvas {
                    id: nodeGraphTool
                    anchors.fill: parent
                    controller: root.nodeEffects
                }
            }
        }

        PanelDock {
            id: nodeInspectorDock
            objectName: "nodeInspectorDock"
            uniqueName: root.engineName("NodeInspector")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.NodeEffects", "Node")
            Panel {
                id: nodeInspectorPanel
                objectName: "nodeInspectorPanel"
                dockName: root.engineName("NodeInspector")
                minimumBodySize: nodeInspectorTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.NodeEffects", "Node")
                NodeInspector {
                    id: nodeInspectorTool
                    anchors.fill: parent
                    controller: root.nodeEffects
                    colourPicker: root.colourPicker
                }
            }
        }

        PanelDock {
            id: nodeSourcesDock
            objectName: "nodeSourcesDock"
            uniqueName: root.engineName("NodeSources")
            affinities: root.dockAffinities
            title: qsTranslate("HikariSub.NodeEffects", "Source lines")
            Panel {
                id: nodeSourcesPanel
                objectName: "nodeSourcesPanel"
                dockName: root.engineName("NodeSources")
                minimumBodySize: nodeSourcesTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.NodeEffects", "Source lines")
                NodeSourceLines {
                    id: nodeSourcesTool
                    anchors.fill: parent
                    controller: root.nodeEffects
                    video: root.video
                }
            }
        }

        PanelDock {
            id: logDock
            objectName: "logDock"
            uniqueName: root.engineName("Log")
            affinities: root.dockAffinities
            title: qsTr("Log")
            Panel {
                id: logPanel
                objectName: "logPanel"
                dockName: root.engineName("Log")
                minimumBodySize: logView.minimumSize
                anchors.fill: parent
                title: qsTr("Log")
                LogPanel {
                    id: logView
                    objectName: "logView"
                    anchors.fill: parent
                    log: root.log
                }
            }
        }

        Component.onCompleted: {
            Docking.setPanelHeader(root.engineName("Grid"), true, null)
            Docking.setPanelHeader(root.engineName("Reference"), true, referenceTray.toolbar)
            Docking.setPanelHeader(root.engineName("Search"), true, null)
            Docking.setPanelHeader(root.engineName("Log"), true, null)
            Docking.setPanelHeader(root.engineName("Spellchecker"), true, null)
            Docking.setPanelHeader(root.engineName("Original"), true, originalTools)
            Docking.setPanelHeader(root.engineName("Styles"), true, stylesTool.toolbar)
            Docking.setPanelHeader(root.engineName("Template"), false, templateTool.toolbar)
            Docking.setPanelHeader(root.engineName("NodeGraph"), true, nodeGraphTool.toolbar)
            Docking.setPanelHeader(root.engineName("NodeSources"), true, nodeSourcesTool.toolbar)
            Docking.setPanelHeader(root.engineName("NodeInspector"), true, null)
            root.defaultLayout()
        }
    }
    property bool arrangementSettled: false
    function settleArrangement() {
        if (root.arrangementSettled)
            return
        root.arrangementSettled = true
        for (const panel of root.panels)
            panel.reportMinimumSize()
        root.fitAudioBox()
        root.workspaceLayout.captureDefault()
        root.workspaceLayout.buildsArrangement("Timing")
        root.workspaceLayout.buildsArrangement("Typesetting")
        root.workspaceLayout.buildsArrangement("Karaoke")
        root.workspaceLayout.restoreSaved()
        root.placeOriginal()
        Docking.guardClose(root, root.admitClose)
        Docking.followLayoutMinimum(dockingArea.uniqueName)
    }
    onFrameSwapped: settleArrangement()
    Timer { // a hidden window swaps no frames
        interval: 1000
        running: !root.arrangementSettled
        onTriggered: root.settleArrangement()
    }
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.workspaceLayout.save()
    }

    header: ColumnLayout {
        objectName: "windowTop"
        spacing: 0
        DocumentTabBar {
            id: documentTabBar
            app: root.app
            review: root.review
            shellWindow: root.shellWindow
            Layout.fillWidth: true
            onCloseRequested: index => root.closeTab(index)
            onCloseTabsRequested: (index, toTheRight) => root.closeTabs(index, toTheRight)
            onSaveRequested: id => root.saveSubtitles(id)
            onSaveAllRequested: root.saveAllTabs()
            onCloseAllRequested: tabCommands.confirmCloseAll()
        }
        Pane {
            id: layoutNotice
            objectName: "layoutNotice"
            visible: root.workspaceLayout.notice.length > 0
            padding: 6
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    objectName: "layoutNoticeText"
                    text: root.workspaceLayout.notice
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                }
                IconTextButton {
                    iconRole: "layout-restore"
                    text: qsTranslate("HikariSub.Workspace", "Restore the previous layout")
                    visible: root.workspaceLayout.hasBackup
                    enabled: !root.layoutLocked
                    onClicked: root.workspaceLayout.restoreBackup()
                }
                Button {
                    text: qsTranslate("HikariSub.Workspace", "Dismiss")
                    onClicked: root.workspaceLayout.dismissNotice()
                }
            }
        }
        UpdateReadyBar {
            id: updateReadyBar
            updates: root.updates
            Layout.fillWidth: true
            onRestartRequested: {
                root.updates.installer.restartNow()
                root.beginClose("quit")
            }
        }
        InlineBar {
            id: startupBar
            objectName: "startupBar"
            readonly property StartupOffer offer: sessionWindows.offer
            visible: offer.active && !root.windowHome
            severity: offer.kind === "crash" ? "warning" : "info"
            text: offer.barText
            Layout.fillWidth: true
            onClosed: offer.end()
            IconTextButton {
                iconRole: "last-session"
                objectName: "startupRestore"
                visible: startupBar.offer.restorable
                text: startupBar.offer.kind === "crash" ? qsTranslate("HikariSub.Session", "Restore session")
                                                        : qsTranslate("HikariSub.Session", "Restore")
                onClicked: startupBar.offer.restore()
            }
            IconTextButton {
                iconRole: "autosave"
                objectName: "startupAutosaves"
                visible: startupBar.offer.kind === "crash" && startupBar.offer.autosaveCount > 0
                text: qsTranslate("HikariSub.Session", "Autosaves…")
                onClicked: autosaves.show()
            }
        }
        InlineBar {
            id: projectTrustBar
            objectName: "projectTrustBar"
            property string dismissed: ""
            readonly property string folder: {
                root.projectRevision
                return root.app.projectRoot(root.shellWindow)
            }
            readonly property bool needed: {
                root.projectRevision
                return root.app.projectNeedsTrust(root.shellWindow)
            }
            visible: needed && dismissed !== folder
            iconRole: "trust"
            closable: false
            text: qsTranslate("HikariSub.Project", "This project has its own scripts or plugin settings. Trust “%1” to run them.")
                  .arg(root.projectName)
            Layout.fillWidth: true
            IconTextButton {
                iconRole: "trust"
                objectName: "projectTrust"
                text: qsTranslate("HikariSub.Project", "Trust folder")
                onClicked: root.app.trustProject(root.shellWindow)
            }
            Button {
                objectName: "projectTrustNotNow"
                text: qsTranslate("HikariSub.MainWindow", "Not now")
                onClicked: projectTrustBar.dismissed = projectTrustBar.folder
            }
        }
        InlineBar {
            id: projectOfferBar
            objectName: "projectOfferBar"
            property string folder: ""
            function offer(path, name) {
                folder = path
                text = qsTranslate("HikariSub.Project", "This file is in the project %1.").arg(name)
            }
            visible: folder.length > 0 && root.projectName.length === 0
            Layout.fillWidth: true
            onClosed: folder = ""
            IconTextButton {
                iconRole: "open-project"
                objectName: "projectOfferOpen"
                text: qsTranslate("HikariSub.Project", "Open project")
                onClicked: {
                    root.app.openFolder(projectOfferBar.folder, root.shellWindow, 2)
                    projectOfferBar.folder = ""
                }
            }
        }
        InlineBar {
            objectName: "mkvProgress"
            visible: root.matroska.state === 3
                     && (root.mkvTab === "home" ? root.windowHome : !root.windowHome && root.currentTab().id === root.mkvTab)
            closable: false
            text: qsTranslate("HikariSub.Matroska", "Loading subtitles from %1").arg(root.matroska.source)
            Layout.fillWidth: true
            ProgressBar {
                objectName: "mkvProgressBar"
                from: 0
                to: 100
                value: root.matroska.progress
                Layout.preferredWidth: 180
            }
            Label {
                objectName: "mkvElapsed"
                text: root.matroska.elapsed
                color: Theme.muted
            }
            Button {
                objectName: "mkvCancel"
                text: qsTranslate("HikariSub.Matroska", "Cancel")
                onClicked: root.matroska.cancel()
            }
        }
        InlineBar {
            id: diskChangeBar
            objectName: "diskChangeBar"
            readonly property var tab: root.currentTab()
            visible: tab.id !== undefined && root.changedOnDisk[tab.id] === true
            severity: tab.modified ? "warning" : "info"
            closable: false
            text: tab.modified
                  ? qsTranslate("HikariSub.Documents", "%1 was changed by another program. Reloading drops the changes you made here.").arg(tab.title)
                  : qsTranslate("HikariSub.Documents", "%1 was changed by another program.").arg(tab.title)
            Layout.fillWidth: true
            IconTextButton {
                iconRole: "refresh"
                objectName: "reloadYes"
                text: qsTranslate("HikariSub.Documents", "Reload")
                onClicked: {
                    const id = diskChangeBar.tab.id
                    if (root.app.reloadTarget())
                        root.endDiskChange(id)
                }
            }
            IconTextButton {
                iconRole: "compare"
                objectName: "diskChangeCompare"
                text: qsTranslate("HikariSub.Documents", "Compare")
                onClicked: root.app.compareWithFileOnDisk()
            }
            Button {
                objectName: "reloadNo"
                text: qsTranslate("HikariSub.Documents", "Keep mine")
                onClicked: {
                    if (root.app.acceptFileOnDisk(diskChangeBar.tab.id))
                        root.endDiskChange(diskChangeBar.tab.id)
                    else
                        root.notify("warning", qsTranslate("HikariSub.Documents", "Keep mine"),
                                    qsTranslate("HikariSub.Editor", "The file changed on disk since it was opened. Reload it, or save elsewhere."))
                }
            }
        }
        ColumnLayout {
            objectName: "unresolvedBars"
            visible: entryBars.count > 0
            spacing: 0
            Layout.fillWidth: true
            Repeater {
                id: entryBars
                model: root.app.unresolvedRestores.filter(e => e.tab === root.app.currentTab)
                delegate: InlineBar {
                    id: entryBar
                    required property var modelData
                    required property int index
                    objectName: "unresolvedBar" + index
                    severity: "warning"
                    closable: false
                    text: sessionWindows.entryText(modelData)
                    Layout.fillWidth: true
                    IconTextButton {
                        iconRole: "refresh"
                        objectName: "restoreRetry" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Retry")
                        onClicked: sessionWindows.retry(entryBar.modelData)
                    }
                    IconTextButton {
                        iconRole: "folder-open"
                        objectName: "restoreRelink" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Relink…")
                        onClicked: sessionWindows.relink(entryBar.modelData)
                    }
                    IconTextButton {
                        iconRole: "remove"
                        objectName: "restoreRemove" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Remove")
                        onClicked: sessionWindows.remove(entryBar.modelData)
                    }
                }
            }
        }
        NodeModeBar {
            objectName: "nodeModeBar"
            visible: root.workspaceLayout.nodeEffects
            Layout.fillWidth: true
            controller: root.nodeEffects
            onLeave: root.setNodeEffects(false)
            onProblemChosen: node => {
                root.nodeEffects.selection = [node]
                nodeGraphTool.centreOn(node)
                root.focusPanel(nodeGraphPanel, Qt.OtherFocusReason)
            }
        }
    }

    footer: StatusBar {
        id: statusStrip
        shell: root.shell
        fieldsController: root.statusBar
        selectionText: root.shell.selectionStatus
        saveText: root.editor.saveStatus
        reviewText: root.review.on && root.review.waiting > 0
                    ? qsTranslate("HikariSub.Review", "%n AI change(s) to review", "", root.review.waiting) : ""
        onReviewClicked: root.showPanel(reviewDock)
    }
    function currentTab() {
        const tabs = root.app.tabs
        return root.app.currentTab >= 0 && root.app.currentTab < tabs.length ? tabs[root.app.currentTab] : ({})
    }
    function pickerFolder(picker) {
        const rows = root.app.settings.value("workspace.pickerFolders") || []
        const row = rows.find(r => r.startsWith(picker + "\t"))
        return row ? row.slice(picker.length + 1) : ""
    }
    function startFolder(picker, folder) {
        return folder.toString() !== "" ? folder : root.pickerFolder(picker)
    }
    function rememberPickerFolder(picker, file) {
        const folder = root.app.folderOf(file)
        if (folder.length === 0)
            return
        const rows = (root.app.settings.value("workspace.pickerFolders") || []).filter(r => !r.startsWith(picker + "\t"))
        rows.push(picker + "\t" + folder)
        root.app.settings.setValue("workspace.pickerFolders", rows)
    }
    function tabTitle(id) {
        if (!id)
            return root.currentTab().title || ""
        const row = root.app.tabs.find(t => t.id === id)
        return row ? row.title : ""
    }
    function notify(severity, title, text, options) {
        return notices.post(severity, title, text, options)
    }
    ToastStack {
        id: notices
        objectName: "toastStack"
        log: root.log
        settings: root.app.settings
        shellWindow: root.shellWindow
        activeWindow: root.app.lastUsedWindow === root.shellWindow
        readonly property bool inFullscreen: root.videoFullscreen.active
        parent: inFullscreen ? videoFullscreenWindow.contentItem : root.Overlay.overlay.parent
        anchors.right: parent ? parent.right : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        anchors.rightMargin: 12
        anchors.bottomMargin: 12 + (inFullscreen ? videoFullscreenWindow.pinnedPanelHeight : root.footer.height)
        z: 1000002
        AutomationProgress {
            id: automationProgress
            width: parent ? parent.width : implicitWidth
            automation: root.automation
            onOpenRun: runId => root.openAutomationRun(runId)
            onFocusReturned: panel => {
                if (panel)
                    root.focusPanel(panel, Qt.OtherFocusReason)
                else
                    root.contentItem.forceActiveFocus(Qt.OtherFocusReason)
            }
        }
    }
    readonly property alias notices: notices
    Connections {
        target: root.starPrompt
        function onDue() {
            if (!root.starPrompt.offer(root.video.playing || root.audio.playing, root.videoFullscreen.active,
                                       root.workspaceLayout.modalOpen))
                return
            notices.show(0, 0, qsTr("Enjoying HikariSub?"), qsTr("A star on GitHub helps others find it."), "", [
                { text: qsTr("Star on GitHub"), run: () => root.starPrompt.star() },
                { text: qsTr("Not now"), run: () => {} }
            ])
        }
    }
    Connections {
        target: root.log
        function onRevealRequested() {
            if (root.videoFullscreen.active)
                root.videoFullscreen.leave()
            root.showPanel(logDock)
        }
    }
    MessageHost {
        id: messages
        objectName: "messages"
        settings: root.app.settings
    }
    readonly property alias messages: messages
    Loader {
        id: saveLayoutDialogLoader
        objectName: "saveLayoutDialogLoader"
        active: false
        sourceComponent: SaveLayoutDialog {
            id: saveLayoutDialog
            layouts: root.workspaceLayout
            settings: root.app.settings
        }
    }
    Loader {
        id: manageLayoutsDialogLoader
        objectName: "manageLayoutsDialogLoader"
        active: false
        sourceComponent: ManageLayoutsDialog {
            id: manageLayoutsDialog
            layouts: root.workspaceLayout
            settings: root.app.settings
        }
    }
    PluginManagerDialog {
        id: pluginManagerDialog
        manager: root.app.pluginManager
        market: root.app.pluginMarketplace
        runtime: root.app.pluginRuntime
        settings: root.app.settings
        onCopyRequested: text => root.app.copyText(text)
    }
    Connections {
        target: root.app.pluginManager
        enabled: notices.activeWindow
        function onRolledBack(id, name, version) {
            root.notices.post("info", qsTranslate("HikariSub.Plugins", "%1 rolled back to %2").arg(name, version),
                              qsTranslate("HikariSub.Plugins", "Automatic updates skip the version you left."),
                              {actions: [{text: qsTranslate("HikariSub.Plugins", "Undo"), run: () => root.app.pluginManager.undoRollBack(id)}]})
        }
    }
    Connections {
        target: root.app.pluginMarketplace
        enabled: notices.activeWindow
        function onRevoked(id, name, version, reason) {
            root.notices.post("warning", qsTranslate("HikariSub.Plugins", "%1 turned off").arg(name),
                              qsTranslate("HikariSub.Plugins", "The marketplace revoked version %1: %2.").arg(version, reason),
                              {actions: [{text: qsTranslate("HikariSub.Plugins", "Show"), run: () => pluginManagerDialog.showPlugin(id)}]})
        }
    }

    Loader {
        id: tapCalibrationWindowLoader
        objectName: "tapCalibrationWindowLoader"
        active: false
        sourceComponent: TapCalibrationWindow {
            id: tapCalibrationWindow
            audio: root.audio
        }
    }

    Loader {
        id: historyWindowLoader
        objectName: "historyWindowLoader"
        active: false
        sourceComponent: HistoryWindow {
            id: historyWindow
            editor: root.editor
            review: root.review
            documentName: root.shell.editingTitle
            target: {
                const tab = root.app.tabs[root.app.currentTab]
                return tab ? tab.id : null
            }
            settings: root.app.settings
        }
    }

    function saveSubtitles(id, overDisk) {
        const document = id ?? 0
        const route = root.app.saveRouteFor(document)
        const marked = document === 0 ? root.currentTab().id : document
        if (!overDisk && route !== "dialog" && route !== "readonly" && root.changedOnDisk[marked] === true)
            diskSaveQuestion.confirm([marked], () => root.saveSubtitles(id, true))
        else if (route === "dialog")
            openSaveDialog(document)
        else if (route === "readonly")
            root.warnReadOnly(document, root.tabTitle(document))
        else
            confirmScriptInfo([document], () => document === 0 ? root.editor.save() : root.app.saveDocument(document), () => {})
        return true
    }
    function confirmScriptInfo(ids, then, cancelled) {
        if (ids.length === 0) {
            then()
            return
        }
        const document = ids[0]
        const rest = () => root.confirmScriptInfo(ids.slice(1), then, cancelled)
        const rows = root.app.scriptInfoQuestion(document)
        if (rows.length === 0) {
            rest()
            return
        }
        scriptInfoOnSaveLoader.active = true
        scriptInfoOnSaveLoader.item.ask(root.tabTitle(document), rows, null, (choice, keys) => {
            if (choice === "cancel") {
                cancelled()
                return
            }
            root.app.approveScriptInfo(document, keys)
            rest()
        })
    }
    Loader {
        id: scriptInfoOnSaveLoader
        active: false
        sourceComponent: ScriptInfoOnSaveDialog {
            settings: root.app.settings
        }
    }
    property var saveQueue: []
    function saveAllTabs(overDisk) {
        const marked = root.app.tabs.filter(t => t.modified && root.changedOnDisk[t.id] === true
                                                 && root.app.saveRouteFor(t.id) === "").map(t => t.id)
        if (!overDisk && marked.length > 0) {
            diskSaveQuestion.confirm(marked, () => root.saveAllTabs(true))
            return
        }
        saveQueue = root.app.saveAll()
        saveNext()
    }
    function saveNext() {
        if (saveQueue.length === 0)
            return
        const next = saveQueue[0]
        saveQueue = saveQueue.slice(1)
        if (next.route === "readonly")
            warnReadOnly(next.id, root.tabTitle(next.id))
        else if (next.route === "scriptInfo")
            confirmScriptInfo([next.id], () => {
                root.app.saveDocument(next.id)
                root.saveNext()
            }, () => root.saveNext())
        else
            openSaveDialog(next.id)
    }
    function saveChosenAs(document, file, format) {
        const result = root.app.saveChosenFor(document, file, format)
        if (result === "readonly") {
            root.warnReadOnly(document, root.app.localPath(file).replace(/^.*[\\/]/, ""))
        } else {
            const id = document === 0 ? root.currentTab().id : document
            if (result === "" && root.changedOnDisk[id] === true)
                root.savingAway = Object.assign({}, root.savingAway, { [id]: true })
            matroskaSubtitles.saveDone()
            root.saveNext()
        }
    }
    function confirmAssConversion(document, file, format, from, then, cancelled) {
        const preview = root.app.assConversionPreview(document, file, format)
        if (preview.convert !== true) {
            then()
            return
        }
        const title = qsTranslate("HikariSub.Documents", "Save as ASS")
        if (preview.problem !== undefined) {
            root.messages.ask({ title: title, text: preview.problem, severity: "error", from: from }, () => cancelled())
            return
        }
        root.messages.ask({
            title: title,
            text: qsTranslate("HikariSub.Documents", "Convert “%1” to ASS before saving it?").arg(root.tabTitle(document)),
            detail: preview.changes.join("\n"),
            plainText: true,
            from: from,
            choices: [
                { id: "convert", text: qsTranslate("HikariSub.Documents", "Convert to ASS"), role: "accept", isDefault: true },
                { id: "cancel", text: qsTranslate("HikariSub.Documents", "Cancel"), role: "reject" }
            ]
        }, id => id === "convert" ? then() : cancelled())
    }
    function openSaveDialog(id) {
        const v = root.app.saveDialogValuesFor(id ?? 0)
        saveAsDialog.document = id ?? 0
        saveAsDialog.formats = v.formats
        saveAsDialog.title = qsTranslate("HikariSub.Documents", "Save “%1” as").arg(root.tabTitle(id ?? 0))
        saveAsDialog.nameFilters = v.filters.concat([qsTranslate("HikariSub.Documents", "All files (*)")])
        if (v.folder.toString() !== "")
            saveAsDialog.currentFolder = v.folder
        else if (root.pickerFolder("subtitles").length > 0)
            saveAsDialog.currentFolder = root.pickerFolder("subtitles")
        saveAsDialog.selectedFile = v.file
        saveAsDialog.open()
    }
    FileChooser {
        id: saveAsDialog
        objectName: "saveAsDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTranslate("HikariSub.Documents", "Subtitle file (%1)").arg("*.ass")]
        // MUST KILL: document id 0 means the editing target
        property var document: 0
        property var formats: []
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            const target = document
            const file = selectedFile
            const format = formats[selectedNameFilter.index] ?? ""
            const cancelled = () => {
                matroskaSubtitles.saveDone()
                root.saveNext()
            }
            root.confirmAssConversion(target, file, format, null, () => root.confirmScriptInfo([target], () => root.saveChosenAs(target, file, format), cancelled), cancelled)
        }
        onRejected: {
            matroskaSubtitles.saveDone()
            root.saveNext()
        }
    }
    MatroskaSubtitles {
        id: matroskaSubtitles
        objectName: "matroskaSubtitles"
        matroska: root.matroska
        settings: root.app.settings
        notify: root.notify
        documentTitle: () => root.currentTab().title || ""
        videoName: () => root.currentTab().video || ""
        save: function() {
            const route = root.app.saveRoute()
            const asks = root.changedOnDisk[root.currentTab().id] === true
            root.saveSubtitles()
            return route === "dialog" || route === "readonly" || asks
        }
    }
    function warnReadOnly(document, name) {
        root.messages.ask({
            title: qsTranslate("HikariSub.Documents", "Save"),
            text: qsTranslate("HikariSub.Documents", "“%1” is read-only.").arg(name ?? root.tabTitle(document)),
            detail: qsTranslate("HikariSub.Documents", "Save it under another name, or make the file writable and save again."),
            severity: "warning",
            choices: [
                { id: "saveAs", text: qsTranslate("HikariSub.Documents", "Save as…"), role: "accept", isDefault: true },
                { id: "cancel", text: qsTranslate("HikariSub.Documents", "Cancel"), role: "reject" }
            ]
        }, id => {
            if (id === "saveAs") {
                root.openSaveDialog(document)
            } else {
                matroskaSubtitles.saveDone()
                root.saveNext()
            }
        })
    }

    LazyMessageBox {
        id: diskSaveQuestion
        name: "diskSaveQuestion"
        settings: root.app.settings
        function confirm(ids, save, cancelled) {
            const names = ids.map(id => "“" + root.tabTitle(id) + "”").join(", ")
            ask({
                title: qsTranslate("HikariSub.Documents", "Save"),
                text: ids.length === 1
                      ? qsTranslate("HikariSub.Documents", "%1 was changed by another program.").arg(names)
                      : qsTranslate("HikariSub.Documents", "%1 were changed by another program.").arg(names),
                detail: qsTranslate("HikariSub.Documents", "Saving writes over those changes."),
                severity: "warning",
                choices: [
                    { id: "saveOver", text: qsTranslate("HikariSub.Documents", "Save over it"), role: "destructive" },
                    { id: "cancel", text: qsTranslate("HikariSub.Documents", "Cancel"), role: "reject", isDefault: true }
                ]
            }, id => {
                const unread = id === "saveOver" ? ids.filter(marked => !root.app.acceptFileOnDisk(marked)) : []
                if (id === "saveOver" && unread.length === 0) {
                    for (const marked of ids)
                        root.endDiskChange(marked)
                    matroskaSubtitles.awaitWrite(ids, root.currentTab().id)
                    if (save() === false)
                        matroskaSubtitles.saveCancelled()
                } else if (id === "saveOver") {
                    root.notify("warning", qsTranslate("HikariSub.Documents", "Save"),
                                qsTranslate("HikariSub.Editor", "The file changed on disk since it was opened. Reload it, or save elsewhere."))
                    if (cancelled)
                        cancelled()
                } else if (cancelled) {
                    cancelled()
                }
                if (id !== "saveOver" || unread.length > 0)
                    matroskaSubtitles.saveCancelled()
            })
        }
    }

    TabCommands {
        id: tabCommands
        objectName: "tabCommands"
        app: root.app
        onCloseAllConfirmed: root.beginClose("all")
        onSubtitlesWithVideo: (subtitles, video) => {
            const result = root.app.reviewOpenWithVideo(subtitles, video)
            if (!result.ok)
                return
            if (result.rows.length === 0)
                root.app.finishClose()
            else
                closeReview.review(result.rows)
        }
    }

    UnsavedChanges {
        id: closeReview
        objectName: "unsavedChanges"
        app: root.app
        updates: root.updates
        settings: root.app.settings
        pickerFolder: picker => root.pickerFolder(picker)
        rememberPickerFolder: (picker, file) => root.rememberPickerFolder(picker, file)
        confirmConversion: (id, file, format, from, then, cancelled) => root.confirmAssConversion(id, file, format, from, then, cancelled)
        confirmOverDisk: (ids, then, cancelled) => {
            const marked = ids.filter(id => root.changedOnDisk[id] === true)
            if (marked.length === 0)
                then()
            else
                diskSaveQuestion.confirm(marked, then, cancelled)
        }
        confirmScriptInfo: (ids, then, cancelled) => root.confirmScriptInfo(ids, then, cancelled)
    }

    AutomationDialog {
        objectName: "automationDialog"
        controller: root.automationDialogs
        colourPicker: root.colourPicker
        settings: root.app.settings
        messages: messages
    }
    AutomationFilePicker {
        picker: root.automationPicker
    }
    PluginFilePicker {
        ui: root.pluginUi
        messages: root.messages
    }
    ScriptResolutionDialog {
        prompt: root.automationResolution
    }
    AutomationNotices {
        automation: root.automation
        active: notices.activeWindow
        toasts: root.notices
        messages: root.messages
    }
    FileChooser {
        id: scriptDialog
        nameFilters: [qsTranslate("HikariSub.Automation", "Automation scripts (*.lua *.moon)"), qsTranslate("HikariSub.Automation", "All files (*)")]
        onAccepted: root.automation.loadScript(selectedFile)
    }

    FileChooser {
        id: videoDialog
        objectName: "videoOpenDialog"
        nameFilters: [qsTranslate("HikariSub.Video", "Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"), qsTranslate("HikariSub.Video", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("video", selectedFile)
            if (root.videoFullscreen.active)
                root.openVideoUnasked(root.app.localPath(selectedFile))
            else
                tabCommands.openVideoFile(root.app.localPath(selectedFile))
        }
        function show() {
            currentFolder = root.startFolder("video", root.app.videoDialogFolder())
            open()
        }
    }
    FileChooser {
        id: lyricsDialog
        objectName: "lyricsOpenDialog"
        title: qsTranslate("HikariSub.Documents", "Import lyrics")
        nameFilters: [qsTranslate("HikariSub.Documents", "Lyrics (*.lrc *.krc *.qrc *.ttml)"),
                      qsTranslate("HikariSub.Documents", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("lyrics", selectedFile)
            root.openLyricsImport(root.app.localPath(selectedFile))
        }
        function show() {
            const folder = root.pickerFolder("lyrics")
            if (folder.length > 0)
                currentFolder = folder
            open()
        }
    }
    Loader {
        id: lyricsImportLoader
        active: false
        sourceComponent: LyricsImportDialog {
            controller: root.lyricsImport
            settings: root.app.settings
            onChooseAnother: lyricsDialog.show()
        }
    }
    function openLyricsImport(path) {
        if (!root.lyricsImport.load(path)) {
            root.notices.post("error", qsTranslate("HikariSub.Documents", "Import lyrics"),
                              qsTranslate("HikariSub.Documents", "Could not read %1.").arg(path))
            return
        }
        lyricsImportLoader.active = true
        lyricsImportLoader.item.open()
    }
    Loader {
        id: dummyVideoLoader
        active: false
        sourceComponent: DummyVideoDialog {
            app: root.app
            settings: root.app.settings
            colourPicker: root.colourPicker
            onRefused: message => root.notices.post("error", qsTr("Open dummy video"), message)
        }
    }
    function openDummyVideo() {
        dummyVideoLoader.active = true
        dummyVideoLoader.item.show()
    }
    Loader {
        id: videoDetailsLoader
        active: false
        sourceComponent: VideoDetailsDialog {
            video: root.video
            timecodesFile: root.app.timecodesFile
            settings: root.app.settings
        }
    }
    function openVideoDetails() {
        videoDetailsLoader.active = true
        videoDetailsLoader.item.show()
    }
    VideoFileQuestion {
        id: videoFileQuestion
        app: root.app
        video: root.video
        messages: root.messages
    }
    TranslationModeQuestion {
        id: translationModeQuestion
        app: root.app
    }
    TranslationModeOffConfirm {
        id: translationModeOffConfirm
        app: root.app
    }

    Loader {
        id: aspectRatioDialogLoader
        objectName: "aspectRatioDialogLoader"
        active: false
        sourceComponent: AspectRatioDialog {
            id: aspectRatioDialog
            view: root.videoView
            video: root.video
            settings: root.app.settings
        }
    }

    VideoFullscreen {
        id: videoFullscreenWindow
        shell: root
        controller: root.videoFullscreen
        onOpenVideoRequested: videoDialog.show()
        onOpenSubtitlesRequested: openDialog.show()
        onAspectRatioRequested: root.lazy(aspectRatioDialogLoader).openAtCursor(videoFullscreenWindow.contentItem)
        onFileQuestionRequested: next => videoFileQuestion.ask(next, videoFullscreenWindow)
        onAudioSettingsRequested: {
            root.videoFullscreen.leave()
            root.openSettings("audio")
        }
        Component.onCompleted: {
            root.videoFullscreen.setWindows(videoFullscreenWindow, root)
            root.videoFullscreen.setFocusFallback(videoPanel)
        }
    }
    Binding {
        when: root.videoFullscreen.active
        target: videoDialog; property: "parentWindow"; value: videoFullscreenWindow
    }
    Binding {
        when: root.videoFullscreen.active
        target: openDialog; property: "parentWindow"; value: videoFullscreenWindow
    }
    Binding { target: MenuKeys; property: "hotkeys"; value: root.hotkeys }
    property bool videoDockClosedForFullscreen: false
    // MUST KILL: must be set after videoDock closes, or closing the dock takes the picture back into the panel
    property bool videoStageInFullscreen: false
    Connections {
        target: root.videoFullscreen
        function onActiveChanged() {
            const fs = root.videoFullscreen
            if (fs.active && fs.onAnotherMonitor && videoDock.isOpen) {
                root.videoDockClosedForFullscreen = true
                videoDock.close()
            }
            root.videoStageInFullscreen = fs.active
            if (!fs.active && root.videoDockClosedForFullscreen) {
                root.videoDockClosedForFullscreen = false
                videoDock.open()
            }
            // A frame submitted just before the move to another window is refused there (its surface changed), so submit again.
            Qt.callLater(() => root.video.attachPresenter(presenter))
        }
    }

    FileChooser {
        id: openDialog
        objectName: "subtitlesOpenDialog"
        title: qsTranslate("HikariSub.Documents", "Open subtitles")
        function show() {
            const folder = root.pickerFolder("subtitles")
            if (folder.length > 0)
                currentFolder = folder
            open()
        }
        nameFilters: [qsTranslate("HikariSub.Documents", "Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"),
                      qsTranslate("HikariSub.Documents", "Word-timed lyrics (*.lrc *.krc *.qrc *.ttml)"),
                      qsTranslate("HikariSub.Documents", "YouTube timed text (*.ytt *.srv3)"),
                      qsTranslate("HikariSub.Documents", "Videos with embedded subtitles (*.mkv *.ogm)"),
                      qsTranslate("HikariSub.Documents", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            root.openChosenSubtitles(root.app.localPath(selectedFile))
        }
    }

    FolderChooser {
        id: projectFolderDialog
        objectName: "projectFolderDialog"
        title: qsTranslate("HikariSub.Project", "Open folder")
        function show() {
            const folder = root.pickerFolder("project")
            if (folder.length > 0)
                currentFolder = folder
            open()
        }
        onAccepted: {
            root.rememberPickerFolder("project", selectedFolder)
            root.app.openFolder(root.app.localPath(selectedFolder), root.shellWindow)
        }
    }

    Loader {
        id: setNewFpsLoader
        active: false
        sourceComponent: SetNewFpsWindow {
            app: root.app
            settings: root.app.settings
        }
    }
    function openSetNewFps() {
        setNewFpsLoader.active = true
        setNewFpsLoader.item.show()
    }

    Loader {
        id: columnsWindowLoader
        objectName: "columnsWindowLoader"
        active: false
        sourceComponent: ColumnsWindow {
            id: columnsWindow
            app: root.app
            settings: root.app.settings
        }
    }

    DialogWindow {
        id: placementWindow
        objectName: "placementWindow"
        title: qsTranslate("HikariSub.Workspace", "Move panel")
        dialogId: "movePanel"
        settings: root.app.settings
        initialFocus: placementPanel
        defaultButton: placementClose
        function openFor(dock) {
            if (root.layoutLocked)
                return
            placementPanel.currentIndex = Math.max(0, root.dockList.indexOf(dock))
            refreshSize()
            open()
            placementPanel.forceActiveFocus()
        }
        function refreshSize() {
            const size = root.workspaceLayout.panelSize(root.dockList[placementPanel.currentIndex])
            placementWidth.value = size.width
            placementHeight.value = size.height
        }
        RowLayout {
            spacing: 8
            Layout.preferredWidth: 356
            Label { text: qsTranslate("HikariSub.Workspace", "Panel"); Layout.preferredWidth: 56 }
            ComboBox {
                id: placementPanel
                objectName: "placementPanel"
                Layout.fillWidth: true
                // KDDW's dock title reads empty until its dock widget exists and doesn't notify, so it is read while shown.
                model: root.dockList
                textRole: "title"
                displayText: placementWindow.visible && currentIndex >= 0 ? root.dockList[currentIndex].title : ""
                Accessible.name: qsTranslate("HikariSub.Workspace", "Panel")
                onActivated: placementWindow.refreshSize()
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true; Layout.topMargin: 4 }
        Label { text: qsTranslate("HikariSub.Workspace", "Move"); font.weight: Font.DemiBold; Accessible.role: Accessible.Heading }
        RowLayout {
            spacing: 8
            Label { text: qsTranslate("HikariSub.Workspace", "Place"); Layout.preferredWidth: 56 }
            ComboBox {
                id: placementKind
                objectName: "placementKind"
                Layout.preferredWidth: 130
                model: [qsTranslate("HikariSub.Workspace", "Tab with"), qsTranslate("HikariSub.Workspace", "Left of"), qsTranslate("HikariSub.Workspace", "Above"), qsTranslate("HikariSub.Workspace", "Right of"), qsTranslate("HikariSub.Workspace", "Below"), qsTranslate("HikariSub.Workspace", "Float")]
                Accessible.name: qsTranslate("HikariSub.Workspace", "Placement")
            }
            Label {
                text: qsTranslate("HikariSub.Workspace", "next to")
                color: placementTarget.enabled ? Theme.text : Theme.disabled
                Layout.leftMargin: 4
            }
            ComboBox {
                id: placementTarget
                objectName: "placementTarget"
                Layout.fillWidth: true
                enabled: placementKind.currentIndex !== 5
                model: root.dockList
                textRole: "title"
                displayText: placementWindow.visible && currentIndex >= 0 ? root.dockList[currentIndex].title : ""
                currentIndex: root.dockList.indexOf(gridDock)
                Accessible.name: qsTranslate("HikariSub.Workspace", "Next to panel")
            }
        }
        Button {
            objectName: "placementMove"
            text: qsTranslate("HikariSub.Workspace", "Move")
            enabled: root.panelAvailable(root.dockList[placementPanel.currentIndex])
            Layout.alignment: Qt.AlignRight
            onClicked: {
                root.placePanel(root.dockList[placementPanel.currentIndex], placementKind.currentIndex,
                                root.dockList[placementTarget.currentIndex])
                placementWindow.refreshSize()
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true; Layout.topMargin: 4 }
        Label { text: qsTranslate("HikariSub.Workspace", "Resize"); font.weight: Font.DemiBold; Accessible.role: Accessible.Heading }
        RowLayout {
            spacing: 8
            Label { text: qsTranslate("HikariSub.Workspace", "Width"); Layout.preferredWidth: 56 }
            SpinBox {
                id: placementWidth
                objectName: "placementWidth"
                from: 0; to: 10000; stepSize: 10; editable: true
                Layout.preferredWidth: 110
                Accessible.name: qsTranslate("HikariSub.Workspace", "Width")
            }
            Label {
                text: qsTranslate("HikariSub.Workspace", "Height")
                Layout.leftMargin: 10
            }
            SpinBox {
                id: placementHeight
                objectName: "placementHeight"
                from: 0; to: 10000; stepSize: 10; editable: true
                Layout.preferredWidth: 110
                Accessible.name: qsTranslate("HikariSub.Workspace", "Height")
            }
            Label { text: qsTranslate("HikariSub.Workspace", "px"); color: Theme.muted }
        }
        Button {
            objectName: "placementResize"
            text: qsTranslate("HikariSub.Workspace", "Resize")
            Layout.alignment: Qt.AlignRight
            onClicked: {
                root.workspaceLayout.resizePanel(root.dockList[placementPanel.currentIndex],
                                                 placementWidth.value, placementHeight.value)
                placementWindow.refreshSize()
            }
        }
        buttons: [
            PrimaryButton {
                id: placementClose
                objectName: "placementClose"
                text: qsTranslate("HikariSub.Workspace", "Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: placementWindow.close()
            }
        ]
    }

    DialogWindow {
        id: restoreCheckpointDialog
        objectName: "restoreCheckpointDialog"
        title: qsTranslate("HikariSub.Review", "Restore checkpoint")
        dialogId: "restoreCheckpoint"
        settings: root.app.settings
        modal: true
        defaultButton: restoreCheckpointButton
        property var turn: 0
        property var documents: []
        readonly property int editedSince: documents.reduce((n, d) => n + d.editedSince, 0)
        function openFor(turn) {
            restoreCheckpointDialog.turn = turn
            documents = root.review.checkpoint(turn)
            alsoMyEdits.checked = false
            open()
        }
        Label {
            Layout.preferredWidth: 420
            wrapMode: Text.WordWrap
            text: qsTranslate("HikariSub.Review", "Every AI change from this turn on is undone, kept ones too, in every tab it touched, as one history step per tab.")
        }
        Repeater {
            model: restoreCheckpointDialog.documents
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: modelData.title
                }
                Label {
                    color: Theme.muted
                    text: modelData.others > 0
                          ? qsTranslate("HikariSub.Review", "%n line(s)", "", modelData.lines) + ", "
                            + qsTranslate("HikariSub.Review", "%n other change(s)", "", modelData.others)
                          : qsTranslate("HikariSub.Review", "%n line(s)", "", modelData.lines)
                }
            }
        }
        CheckBox {
            id: alsoMyEdits
            objectName: "restoreAlsoMyEdits"
            visible: restoreCheckpointDialog.editedSince > 0
            text: qsTranslate("HikariSub.Review", "Also undo my edits to %n line(s) I changed after the AI", "",
                              restoreCheckpointDialog.editedSince)
        }
        Label {
            Layout.preferredWidth: 420
            wrapMode: Text.WordWrap
            color: Theme.muted
            text: qsTranslate("HikariSub.Review", "Your other edits stay. Undo brings the AI’s changes back.")
        }
        buttons: [
            PrimaryButton {
                id: restoreCheckpointButton
                objectName: "restoreCheckpointButton"
                text: qsTranslate("HikariSub.Review", "Restore")
                enabled: restoreCheckpointDialog.documents.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    root.review.restore(restoreCheckpointDialog.turn, alsoMyEdits.checked)
                    restoreCheckpointDialog.close()
                }
            },
            Button {
                objectName: "restoreCheckpointCancel"
                text: qsTranslate("HikariSub.Review", "Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: restoreCheckpointDialog.close()
            }
        ]
    }

    FileChooser {
        id: audioDialog
        objectName: "audioDialog"
        title: qsTranslate("HikariSub.Audio", "Choose audio file")
        nameFilters: [qsTranslate("HikariSub.Audio", "Audio and video files") + " (*.wav *.w64 *.flac *.ac3 *.aac *.ogg *.opus *.mp3 *.mp4 *.m4a *.mka *.mkv *.webm *.avi)",
                      qsTranslate("HikariSub.Audio", "All files") + " (*)"]
        onAccepted: {
            root.rememberPickerFolder("audio", selectedFile)
            root.audio.openAudioUrl(selectedFile)
        }
    }
    ListChooser {
        id: audioTrackChooser
        objectName: "audioTrackChooser"
        settings: root.app.settings
        Connections {
            target: root.audio
            function onTrackChoicesChanged() {
                if (root.audio.trackChoices.length > 0) {
                    audioTrackChooser.choose({
                        title: qsTranslate("HikariSub.Audio", "Choose the audio track"),
                        rows: root.audio.trackChoices,
                        acceptText: qsTranslate("HikariSub.Audio", "Open")
                    }, row => {
                        if (row >= 0)
                            root.audio.chooseTrack(row)
                        else
                            root.audio.cancelTrackChoice()
                    })
                } else if (audioTrackChooser.opened) {
                    audioTrackChooser.done = null
                    audioTrackChooser.none()
                }
            }
        }
    }
    FileChooser {
        id: keyframesDialog
        objectName: "keyframesDialog"
        title: qsTranslate("HikariSub.Video", "Choose keyframes file")
        nameFilters: [qsTranslate("HikariSub.Video", "Keyframes file (*.txt *.pass *.stats *.log)"), qsTranslate("HikariSub.Video", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("keyframes", selectedFile)
            const problem = root.app.openKeyframesFile(selectedFile.toString())
            if (problem.length > 0)
                root.notices.post("error", qsTr("Open keyframes"), problem)
        }
        function show() {
            currentFolder = root.startFolder("keyframes", root.app.keyframesDialogFolder())
            open()
        }
    }
    function openTimecodes(path) {
        const problem = root.app.openTimecodesFile(path)
        if (problem.length > 0)
            root.notices.post("error", qsTranslate("HikariSub.Video", "Open timecodes file"), problem)
    }
    FileChooser {
        id: timecodesDialog
        objectName: "timecodesDialog"
        title: qsTranslate("HikariSub.Video", "Open timecodes file")
        nameFilters: [qsTranslate("HikariSub.Video", "Timecodes file (*.txt)"), qsTranslate("HikariSub.Video", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("timecodes", selectedFile)
            root.openTimecodes(selectedFile.toString())
        }
        function show() {
            currentFolder = root.startFolder("timecodes", root.app.timecodesDialogFolder())
            open()
        }
    }
    FileChooser {
        id: saveTimecodesDialog
        objectName: "saveTimecodesDialog"
        fileMode: FileDialog.SaveFile
        title: qsTranslate("HikariSub.Video", "Save timecodes file")
        nameFilters: [qsTranslate("HikariSub.Video", "Timecodes file (*.txt)"), qsTranslate("HikariSub.Video", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("timecodes", selectedFile)
            const problem = root.app.saveTimecodesFile(selectedFile.toString())
            if (problem.length > 0)
                root.notices.post("error", qsTranslate("HikariSub.Video", "Save timecodes file"), problem)
        }
        function show() {
            currentFolder = root.startFolder("timecodes", root.app.timecodesDialogFolder())
            open()
        }
    }
    FileChooser {
        id: agentExportDialog
        objectName: "agentExportDialog"
        property string thread
        title: qsTranslate("HikariSub.Agent", "Export as Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTranslate("HikariSub.Agent", "Markdown (*.md)")]
        function openFor(id) {
            thread = id
            open()
        }
        onAccepted: {
            if (!root.agent.exportMarkdown(thread, selectedFile))
                root.notices.post("warning", qsTranslate("HikariSub.Agent", "Export as Markdown"),
                                  qsTranslate("HikariSub.Agent", "The thread couldn’t be saved there."))
        }
    }
    FileChooser {
        id: translationFileDialog
        title: qsTranslate("HikariSub.Translation", "Choose subtitle file")
        nameFilters: [qsTranslate("HikariSub.Translation", "Subtitle files (*.ass *.srt *.sub *.txt)")]
        onAccepted: root.app.pasteTranslationFile(selectedFile)
    }
    Loader {
        id: translationShiftWindowLoader
        objectName: "translationShiftWindowLoader"
        active: false
        sourceComponent: TranslationShiftWindow {
            id: translationShiftWindow
            app: root.app
            settings: root.app.settings
        }
    }
    Connections {
        target: root.updates
        enabled: notices.activeWindow
        function onFinished(outcome, release, interactive) {
            if (outcome === "available") {
                root.lazy(updateAvailableLoader).showRelease(release, !interactive)
            } else if (outcome === "current") {
                root.notices.post("info", qsTr("Check for updates"), qsTr("You already have the latest version"))
            } else {
                root.notices.post("warning", qsTr("Check for updates"), qsTr("Cannot check for updates"))
            }
        }
        function onNotice(message, atStart) {
            root.notices.post("error", atStart ? qsTr("Install update") : qsTr("Download and install"),
                              message)
        }
    }
    Connections {
        target: root.updates.installer
        function onReady() { updateAvailableLoader.item?.close() }
    }
    Loader {
        id: updateAvailableLoader
        objectName: "updateAvailableLoader"
        active: false
        sourceComponent: UpdateAvailable {
            id: updateAvailable
            updates: root.updates
            app: root.app
            whatsNew: root.whatsNew
            settings: root.app.settings
        }
    }
    Loader {
        id: whatsNewDialogLoader
        objectName: "whatsNewDialogLoader"
        active: false
        sourceComponent: WhatsNewDialog {
            id: whatsNewDialog
            whatsNew: root.whatsNew
            settings: root.app.settings
        }
    }
    Loader {
        id: aboutDialogLoader
        objectName: "aboutDialogLoader"
        active: false
        sourceComponent: AboutDialog {
            id: aboutDialog
            app: root.app
            updates: root.updates
            settings: root.app.settings
            onReportIssueRequested: root.lazy(reportIssueWindowLoader).openForm()
        }
    }
    CrashReportWindow {
        id: crashReportWindow
        reports: root.crashReports
        app: root.app
        offer: sessionWindows.offer
        settings: root.app.settings
        onAutosavesRequested: autosaves.show()
        onLinkTooLong: root.notices.post("info", qsTranslate("HikariSub.Reports", "Open GitHub issue"),
                                         qsTranslate("HikariSub.Reports", "The report is too long for a link, so it is on the clipboard. Paste it into the form."))
        onBrowserFailed: root.issueBrowserFailed()
    }
    Loader {
        id: reportIssueWindowLoader
        objectName: "reportIssueLoader"
        active: false
        sourceComponent: ReportIssueWindow {
            id: reportIssueWindow
            reports: root.crashReports
            app: root.app
            mainWindow: root
            settings: root.app.settings
            onBrowserFailed: root.issueBrowserFailed()
        }
    }
    function issueBrowserFailed() {
        root.notices.post("warning", qsTranslate("HikariSub.Reports", "Open GitHub issue"),
                          qsTranslate("HikariSub.Reports", "The browser did not open. The issue is on the clipboard. Paste it into a new issue on GitHub."))
    }
    Connections {
        target: root.crashReports
        enabled: notices.activeWindow
        function onFreezeNoticed(seconds) {
            root.notices.post("warning", qsTranslate("HikariSub.Reports", "HikariSub stopped responding for %n seconds", "", seconds),
                              qsTranslate("HikariSub.Reports", "It works again. Send a report of where it was stuck?"),
                              { actions: [{ text: qsTranslate("HikariSub.Reports", "Report…"), run: () => crashReportWindow.showReport("freeze") }] })
        }
        function onSent(source, ok) {
            if (ok) {
                root.notices.post("info", qsTranslate("HikariSub.Reports", "Crash report sent. Thank you."), "")
                return
            }
            const note = crashReportWindow.lastNote
            root.notices.post("warning", qsTranslate("HikariSub.Reports", "Send report"),
                              source === "start" ? qsTranslate("HikariSub.Reports", "The crash report could not be sent. It is kept for the next start.")
                                                 : qsTranslate("HikariSub.Reports", "The crash report could not be sent."),
                              { actions: source === "" ? [] : [{ text: qsTranslate("HikariSub.Reports", "Try again"), run: () => root.crashReports.send(source, note) },
                                                               { text: qsTranslate("HikariSub.Reports", "Open GitHub issue"), run: () => crashReportWindow.openOnGitHub(source, note) }] })
        }
        function onAutoSent(kind, ok, source) {
            if (!ok) {
                root.notices.post("warning", qsTranslate("HikariSub.Reports", "Send report"),
                                  source === "start" ? qsTranslate("HikariSub.Reports", "The crash report could not be sent. It is kept for the next start.")
                                                     : qsTranslate("HikariSub.Reports", "The crash report could not be sent."),
                                  { actions: source === "" ? [] : [{ text: qsTranslate("HikariSub.Reports", "Show report"), run: () => crashReportWindow.showReport(source) }] })
                return
            }
            root.notices.post("info", kind === "freeze" ? qsTranslate("HikariSub.Reports", "HikariSub stopped responding. A freeze report was sent.")
                                                        : qsTranslate("HikariSub.Reports", "HikariSub closed unexpectedly. A crash report was sent."),
                              qsTranslate("HikariSub.Reports", "“Always send crash reports” is on."),
                              { actions: [{ text: qsTranslate("HikariSub.Reports", "Show report"), run: () => crashReportWindow.showReport("sent", kind) },
                                          { text: qsTranslate("HikariSub.Reports", "Settings"), run: () => root.openSettings("updates") }] })
        }
    }
    Loader {
        id: creditsDialogLoader
        objectName: "creditsDialogLoader"
        active: false
        sourceComponent: CreditsDialog {
            id: creditsDialog
            app: root.app
            settings: root.app.settings
        }
    }
    function openResample() {
        resampleLoader.active = true
        resampleLoader.item.openDialog()
    }
    Loader {
        id: resampleLoader
        active: false
        sourceComponent: ResampleWindow {
            app: root.app
            settings: root.app.settings
            video: root.video
        }
    }
    function chooseColumns(paste) { root.lazy(columnsWindowLoader).choose(paste) }
    function lazy(loader) {
        loader.active = true
        return loader.item
    }
    function openMotionData() {
        motionDataLoader.active = true
        motionDataLoader.item.openDialog()
    }
    Loader {
        id: motionDataLoader
        active: false
        sourceComponent: MotionDataWindow {
            app: root.app
            settings: root.app.settings
        }
    }
    function openInterpolateLines() {
        interpolateLinesLoader.active = true
        interpolateLinesLoader.item.openDialog()
    }
    Loader {
        id: interpolateLinesLoader
        active: false
        sourceComponent: InterpolateLinesWindow {
            app: root.app
            shell: root.shell
            settings: root.app.settings
        }
    }
    function openExtendTracking() {
        extendTrackingLoader.active = true
        extendTrackingLoader.item.openDialog()
    }
    Loader {
        id: extendTrackingLoader
        active: false
        sourceComponent: ExtendTrackingWindow {
            app: root.app
            settings: root.app.settings
        }
    }
    function revertMotionData() {
        const failure = root.app.revertMotionData()
        if (failure.length > 0)
            root.notify("warning", failure, "")
    }
    function trackingToast(result) {
        root.notify(result.ok ? "info" : "warning", result.text, "")
    }
    function openBlurAndGlowLayers() {
        blurAndGlowLayersLoader.active = true
        blurAndGlowLayersLoader.item.openDialog()
    }
    function repeatBlurAndGlowLayers() {
        const result = root.app.repeatLayerBuilders()
        root.notify(result.ok ? "info" : "warning", result.text, "")
    }
    Loader {
        id: blurAndGlowLayersLoader
        active: false
        sourceComponent: BlurAndGlowLayersDialog {
            app: root.app
            shell: root.shell
            colourPicker: root.colourPicker
            settings: root.app.settings
        }
    }
    function openPerspectiveTracking() {
        perspectiveTrackingLoader.active = true
        perspectiveTrackingLoader.item.openDialog()
    }
    Loader {
        id: perspectiveTrackingLoader
        active: false
        sourceComponent: PerspectiveTrackingWindow {
            app: root.app
            settings: root.app.settings
        }
    }
    function openScriptProperties() {
        scriptPropertiesLoader.active = true
        return scriptPropertiesLoader.item.openFor()
    }
    Loader {
        id: scriptPropertiesLoader
        active: false
        sourceComponent: ScriptPropertiesDialog {
            app: root.app
            settings: root.app.settings
            video: root.video
        }
    }
    function askResolutionMismatch(sizes) {
        const choices = [{ id: "keep", text: qsTranslate("HikariSub.Conversion", "Keep as is"), role: "reject" },
                         { id: "only", text: qsTranslate("HikariSub.Conversion", "Change resolution only"), role: "action" }]
        if (sizes.videoWidth / sizes.subsWidth !== sizes.videoHeight / sizes.subsHeight)
            choices.push({ id: "stretch", text: qsTranslate("HikariSub.Conversion", "Resample and stretch"), role: "action" })
        choices.push({ id: "resample", text: qsTranslate("HikariSub.Conversion", "Resample"), role: "accept", isDefault: true })
        const document = root.app.editingDocument()
        const shown = () => {
            const now = root.app.resampleValues()
            return root.app.editingDocument() === document && now.hasVideo === true
                && now.videoWidth === sizes.videoWidth && now.videoHeight === sizes.videoHeight
                && now.subsWidth === sizes.subsWidth && now.subsHeight === sizes.subsHeight
        }
        root.messages.ask({
            title: qsTranslate("HikariSub.Conversion", "Open video"),
            text: qsTranslate("HikariSub.Conversion", "The video is %1 × %2 and the subtitles are %3 × %4.")
                      .arg(sizes.videoWidth, sizes.videoHeight, sizes.subsWidth, sizes.subsHeight),
            detail: qsTranslate("HikariSub.Conversion", "Resample the subtitles to the video? Subtitles › Resample subtitles does it later too."),
            severity: "info",
            choices: choices,
            dontAsk: true
        }, (id, dontAskAgain) => {
            if (dontAskAgain)
                root.app.askForBadResolution = false
            const option = ({ only: 0, resample: 1, stretch: 2 })[id]
            if (option !== undefined && shown())
                root.app.matchVideoResolution(option)
        })
    }
    function askVideoProperty(question) {
        const size = s => s.width + " × " + s.height
        const leave = (text, isDefault) => ({ id: "leave", text: text, role: "reject", isDefault: isDefault })
        const set = (id, text, isDefault) => ({ id: id, text: text, role: "action", isDefault: isDefault })
        let o
        switch (question.kind) {
        case "untaggedMatrix":
            o = { title: qsTr("Untagged video"), severity: "warning",
                  text: qsTr("The video doesn’t say which colour matrix it uses."),
                  detail: qsTr("HikariSub guesses %1, but other players may guess differently, so the video’s colours, and subtitle colours matched to them, can differ between players. Tag the video with its colour matrix so that every player shows it the same.").arg(question.recommended) }
            break
        case "hdr":
            o = { title: qsTr("HDR or wide-gamut video"), severity: "warning",
                  text: qsTr("The video has HDR or wide-gamut colours."),
                  detail: qsTr("Ordinary subtitles work on it, but subtitle colours can’t match the video’s colours.") }
            break
        case "missingMatrix":
            o = { title: qsTr("Set the YCbCr matrix?"),
                  text: qsTr("The subtitles have no YCbCr matrix."),
                  detail: qsTr("If they are for this video, set it to the video’s (%1). If they are for another video, leave it unset until you open that one.").arg(question.recommended),
                  choices: [leave(qsTr("Leave unset"), false), set("set", qsTr("Set to %1").arg(question.recommended), true)] }
            break
        case "matrixMismatch":
            o = { title: qsTr("YCbCr matrix mismatch"),
                  text: qsTr("The video’s YCbCr matrix (%1) differs from the subtitles’ (%2).").arg(question.recommended, question.script),
                  detail: qsTr("If this video’s colours look the same as those of the video the subtitles were made for, set the subtitles’ matrix to the video’s."),
                  choices: [leave(qsTr("Keep %1").arg(question.script), false), set("set", qsTr("Set to %1").arg(question.recommended), true)] }
            break
        case "missingLayoutRes":
            o = { title: qsTr("Set the layout resolution?"),
                  text: qsTr("The subtitles have no layout resolution."),
                  detail: qsTr("If they are for this video, set it to the video’s (%1). For an older file, set it to the resolution of the video it was made for, which is often its script resolution (%2).").arg(size(question.videoSize), size(question.playSize)),
                  choices: [leave(qsTr("Leave unset"), false), set("set", qsTr("Set to the video’s (%1)").arg(size(question.videoSize)), true),
                            set("play", qsTr("Set to the script resolution (%1)").arg(size(question.playSize)), false)] }
            break
        case "layoutResMismatch":
            o = { title: qsTr("Layout resolution mismatch"),
                  text: qsTr("The video is %1 and the subtitles’ layout resolution is %2.").arg(size(question.videoSize), size(question.layoutSize)),
                  detail: question.aspectChanged
                          ? qsTr("Their aspect ratios differ. Set the layout resolution to the video’s?")
                          : qsTr("This is usually fine. If the subtitles have no significant formatting yet and you’ll format them on this video, set the layout resolution to the video’s."),
                  choices: [leave(qsTr("Keep %1").arg(size(question.layoutSize)), true), set("set", qsTr("Set to the video’s (%1)").arg(size(question.videoSize)), false)] }
            break
        default:
            return
        }
        o.dontAsk = true
        root.messages.ask(o, (id, dontAskAgain) => {
            const choice = ({ set: 1, play: 2 })[id] ?? 0
            if (dontAskAgain)
                root.app.stopAskingVideoProperty(question.kind, choice)
            if (choice > 0)
                root.app.answerVideoProperty(question, choice)
        })
    }
    Connections {
        target: root.app
        enabled: notices.activeWindow
        function onResolutionMismatch(sizes) { root.askResolutionMismatch(sizes) }
        function onVideoPropertyQuestion(question) { root.askVideoProperty(question) }
    }
    Loader {
        id: styleManagerWindowLoader
        objectName: "styleManagerLoader"
        active: false
        sourceComponent: StyleManager {
            id: styleManagerWindow
            styles: root.styleManager
            catalogs: root.fontCatalogs
            shellWindow: root.shellWindow
            colourPicker: root.colourPicker
            app: root.app
            notices: root.notices
        }
    }
    Loader {
        id: settingsImportDialogLoader
        objectName: "settingsImportDialogLoader"
        active: false
        sourceComponent: SettingsImportDialog {
            id: settingsImportDialog
            importer: root.settingsImport
            settings: root.app.settings
        }
    }
    Loader {
        id: gettingStartedDialogLoader
        objectName: "gettingStartedDialogLoader"
        active: false
        sourceComponent: GettingStartedDialog {
            id: gettingStartedDialog
            app: root.app
            importer: root.settingsImport
            updates: root.updates
            layouts: root.workspaceLayout.presets
            onFinished: layout => {
                if (layout.length > 0)
                    root.applyPreset(layout)
            }
            onSkipped: if (gettingStartedDialog.due) root.notices.post("info", qsTranslate("HikariSub.GettingStarted", "Default settings in use"),
                                                                      qsTranslate("HikariSub.GettingStarted", "Help › Getting started opens the setup again."))
        }
    }
    Loader {
        id: settingsLoader
        objectName: "settingsLoader"
        active: false
        sourceComponent: Component {
            SettingsDialog {
                app: root.app
                shellWindow: root.shellWindow
                notices: root.notices
                hotkeys: root.hotkeys
                updates: root.updates
                crashReports: root.crashReports
                ai: root.aiSettings
                inlineAi: root.inlineAi
                colourPicker: root.colourPicker
                tagButtons: root.tagButtons
                importer: root.settingsImport
                onCreditsRequested: root.lazy(creditsDialogLoader).open()
                onLegacyImportRequested: root.importLegacySettings()
                onLegacyRollBackRequested: root.lazy(settingsImportDialogLoader).openRollBack()
            }
        }
    }
    function settingsWindow() {
        settingsLoader.active = true
        return settingsLoader.item
    }
    function openSettings(page, filter) { settingsWindow().openDialog(page, filter) }
    function repeatInlineAi(inEditor, field) {
        if (root.inlineAi.last.action)
            inlineAiBox.begin(Object.assign({}, root.inlineAi.last, {repeat: true}), inEditor, field)
    }
    function openInlineAiMenu() {
        if (lineText.activeFocus && editorStrip.aiShown) {
            editorAiMenu.popup(editorStrip.aiButton, 0, editorStrip.aiButton.height)
            return
        }
        grid.aiMenuNext = true
        grid.requestMenu()
        grid.aiMenuNext = false
    }
    InlineAiBox {
        id: inlineAiBox
        parent: Overlay.overlay
        controller: root.inlineAi
    }
    function importLegacySettings() {
        if (root.settingsImport.available)
            root.lazy(settingsImportDialogLoader).openDialog()
    }
    Loader {
        id: fontCollectorLoader
        active: false
        sourceComponent: FontCollectorDialog {
            collector: root.fontCollector
            settings: root.app.settings
        }
    }
    function openFontCollector() {
        fontCollectorLoader.active = true
        fontCollectorLoader.item.showOnce()
    }
    Connections {
        target: root.fontCollector
        function onStyleRequested(style) {
            root.lazy(styleManagerWindowLoader).showFor(style)
        }
    }
    Loader {
        id: selectLinesDialogLoader
        objectName: "selectLinesDialogLoader"
        active: false
        sourceComponent: SelectLinesDialog {
            id: selectLinesDialog
            app: root.app
            notices: root.notices
            Connections {
                target: root.app
                function onSelectLinesDestroyed() {
                    if (selectLinesDialog.visible)
                        selectLinesDialog.close()
                }
            }
        }
    }
    Loader {
        id: karaokeCheckLoader
        active: false
        sourceComponent: KaraokeCheckDialog {
            app: root.app
            settings: root.app.settings
        }
    }
    function openKaraokeCheck() {
        karaokeCheckLoader.active = true
        karaokeCheckLoader.item.openDialog()
    }
    Loader {
        id: exportLoader
        active: false
        sourceComponent: ExportDialog { app: root.app; colourPicker: root.colourPicker }
    }
    function openExportDialog() {
        exportLoader.active = true
        exportLoader.item.openDialog()
    }
    Loader {
        id: kanjiTimerLoader
        active: false
        sourceComponent: KanjiTimerDialog {
            timer: root.kanjiTimer
            settings: root.app.settings
        }
    }
    function openKanjiTimer() {
        kanjiTimerLoader.active = true
        kanjiTimerLoader.item.openDialog()
    }
    Loader {
        id: alignToVideoLoader
        active: false
        sourceComponent: AlignToVideoDialog {
            controller: root.alignToVideo
            settings: root.app.settings
        }
    }
    function openAlignToVideo() {
        alignToVideoLoader.active = true
        const problem = alignToVideoLoader.item.openDialog()
        if (problem.length > 0)
            root.notices.post("error", qsTr("Align to video"), problem)
    }
    Loader {
        id: trackColourLoader
        active: false
        sourceComponent: TrackColourDialog {
            controller: root.videoAnalysis
            settings: root.app.settings
        }
    }
    Loader {
        id: findFadeLoader
        active: false
        sourceComponent: FindFadeDialog {
            controller: root.videoAnalysis
            settings: root.app.settings
        }
    }
    function openVideoAnalysis(mode) {
        const loader = mode === "fade" ? findFadeLoader : trackColourLoader
        loader.active = true
        const problem = loader.item.openDialog()
        if (problem.length > 0)
            root.notices.post("error", mode === "fade" ? qsTr("Find fade") : qsTr("Track colour"), problem)
    }
    ListChooser {
        id: fixSuggestions
        objectName: "fixSuggestions"
        settings: root.app.settings
        function openFor(field, role, position, suggestions) {
            const at = field.positionToRectangle(position)
            choose({
                title: qsTranslate("HikariSub.Spelling", "Fix suggestions"),
                rows: suggestions,
                anchor: field,
                at: Qt.rect(at.x, at.y, Math.max(1, at.width), at.height)
            }, row => {
                if (row >= 0 && row < suggestions.length)
                    root.app.replaceEditorMisspell(role, position, suggestions[row])
            })
        }
    }
    FontPicker {
        id: fontPicker
        editor: root.editor
        catalogs: root.fontCatalogs
        shellWindow: root.shellWindow
        settings: root.app.settings
    }
    FontCatalogWindow {
        id: fontCatalogWindow
        catalogs: root.fontCatalogs
        notices: root.notices
        settings: root.app.settings
        Component.onCompleted: FontWindows.catalogWindow = fontCatalogWindow
    }
    FontsFromSubtitlesDialog {
        id: fontsFromSubtitles
        catalogs: root.fontCatalogs
        notices: root.notices
        settings: root.app.settings
        openFiles: root.app.tabs.length
        fileName: root.app.currentTab >= 0 && root.app.currentTab < root.app.tabs.length ? root.app.tabs[root.app.currentTab].title : ""
        Component.onCompleted: FontWindows.fromSubtitles = fontsFromSubtitles
    }
    ColourPopover {
        id: colourPopover
        picker: root.colourPicker
    }
    function colourClick(number, leftClick, field) {
        if (root.colourPicker.switchClicks)
            leftClick = !leftClick
        colourPopover.openFor(editorStrip.colourAnchor(number), root.editor, () => {
            const c = root.editor.beginColour(number, field.role, field.selectionStart, field.selectionEnd)
            if (c.r !== undefined)
                root.app.colourPickerOpened()
            return c
        }, { title: editorStrip.colourName(number), dropper: !leftClick, focus: field })
    }

    Popover {
        id: groupDescriptionPopover
        objectName: "groupDescriptionPopover"
        title: qsTranslate("HikariSub.Grid", "Line group description")
        showTitle: true
        initialFocus: groupDescription
        property var description: 0
        property bool titled: false
        function edit(id, grid, at) {
            description = id
            titled = root.app.groupTitle(id).length > 0
            groupDescription.text = root.app.groupTitle(id)
            const row = grid.rowAt(at.y)
            let top = at.y
            for (let k = 0; k < grid.rowHeight && top > 0 && grid.rowAt(top - 1) === row; ++k)
                --top
            openAt(grid, null, Qt.rect(Math.max(0, at.x - 24), top, 1, grid.rowHeight))
            groupDescription.selectAll()
        }
        onKept: if (groupDescription.text.length > 0) root.app.renameGroup(description, groupDescription.text)
        property var askedFor: null
        function editingTarget() {
            const tab = root.app.tabs[root.app.currentTab]
            return tab ? tab.id : null
        }
        onOpenedChanged: if (opened) askedFor = editingTarget()
        Connections {
            target: root.app
            function onTabsChanged() {
                if (groupDescriptionPopover.opened && groupDescriptionPopover.editingTarget() !== groupDescriptionPopover.askedFor)
                    groupDescriptionPopover.revert()
            }
        }
        TextField {
            id: groupDescription
            objectName: "groupDescription"
            Layout.preferredWidth: 280
            maximumLength: 500
            Accessible.name: groupDescriptionPopover.title
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PrimaryButton {
                objectName: "groupDescriptionRename"
                text: groupDescriptionPopover.titled ? qsTranslate("HikariSub.Grid", "Rename")
                                                     : qsTranslate("HikariSub.Grid", "Add description")
                enabled: groupDescription.text.length > 0
                onClicked: groupDescriptionPopover.keep()
            }
        }
    }

    Connections {
        target: root.app
        function onAegisubFoldsFound(document, folds) {
            if (root.app.windowOfTab(document) !== root.shellWindow)
                return
            root.notices.post("info", qsTranslate("HikariSub.Grid", "Aegisub folds"),
                              qsTranslate("HikariSub.Grid", "This file has %n Aegisub folds.", "", folds), {
                actions: [{ text: qsTranslate("HikariSub.Grid", "Convert to line groups"), run: () => {
                    root.app.activateShellWindow(root.shellWindow)
                    const tab = root.app.tabs.findIndex(t => t.id === document)
                    if (tab >= 0)
                        root.app.selectTab(tab)
                    root.app.convertFoldsToGroups()
                } }]
            })
        }
        function onFoldsConverted(converted, skipped) {
            const done = qsTranslate("HikariSub.Grid", "%n Aegisub folds became line groups.", "", converted)
            root.notices.postForApp("info", qsTranslate("HikariSub.Grid", "Convert Aegisub folds to line groups"), skipped === 0 ? done
                              : done + " " + qsTranslate("HikariSub.Grid", "%n folds were skipped: they overlap a line group or do not pair.", "", skipped))
        }
        function onGroupBreakRefused(description, title) {
            if (!notices.activeWindow)
                return
            if (!description) {
                root.notices.post("warning", qsTranslate("HikariSub.Grid", "This would leave lines outside their line group"),
                            qsTranslate("HikariSub.Grid", "Nothing was changed."))
                return
            }
            groupBreakBox.askedFor = groupBreakBox.editingTarget()
            groupBreakBox.ask({
                title: qsTranslate("HikariSub.Grid", "Line group"),
                severity: "warning",
                text: qsTranslate("HikariSub.Grid", "This would break the line group “%1”.").arg(title),
                detail: qsTranslate("HikariSub.Grid", "Nothing was changed. Remove the group (its description line is deleted) and try again, or cancel."),
                choices: [{ id: "remove", text: qsTranslate("HikariSub.Grid", "Remove group"), role: "destructive" },
                          { id: "cancel", text: qsTranslate("HikariSub.Grid", "Cancel"), role: "reject", isDefault: true }]
            }, answer => {
                if (answer === "remove" && groupBreakBox.editingTarget() === groupBreakBox.askedFor)
                    root.app.removeGroup(description)
            })
        }
    }
    MessageBox {
        id: groupBreakBox
        objectName: "groupBreakBox"
        settings: root.app.settings
        property var askedFor: null
        function editingTarget() {
            const tab = root.app.tabs[root.app.currentTab]
            return tab ? tab.id : null
        }
        Connections {
            target: root.app
            function onTabsChanged() {
                if (groupBreakBox.visible && groupBreakBox.editingTarget() !== groupBreakBox.askedFor)
                    groupBreakBox.close()
            }
        }
    }

    function apiCommand(symbol, run) {
        const target = root.globalActions()[symbol] ?? root.globalItems()[symbol]
        if (!target)
            return null
        if (!run)
            return { enabled: target.enabled, reason: "" }
        if (!target.enabled)
            return false
        if (target.trigger)
            target.trigger()
        else
            target.triggered()
        return true
    }
    function apiPanels(name, action) {
        if (action === "list")
            return root.dockList.map(dock => ({ panel: root.panelName(dock), title: dock.title, open: dock.isOpen,
                                                floating: dock.isFloating, focused: dock.isFocused }))
        const i = root.dockList.findIndex(dock => root.panelName(dock) === name)
        if (i < 0)
            return false
        const dock = root.dockList[i]
        if (action === "open")
            root.showPanel(dock)
        else if (action === "close")
            root.closePanel(dock)
        else if (dock.isOpen)
            root.focusPanel(root.panels[i], Qt.OtherFocusReason)
        else
            return false
        return true
    }
    readonly property bool servesApi: root.app.lastUsedWindow === root.shellWindow
    onServesApiChanged: if (servesApi) root.serveApi()
    function serveApi() {
        root.app.apiHost.commandHandler = root.apiCommand
        root.app.apiHost.panelHandler = root.apiPanels
    }
    function connectApi() {
        const api = root.app.apiHost
        if (root.servesApi)
            root.serveApi()
        for (const symbols of [root.globalActions(), root.globalItems()])
            for (const symbol in symbols)
                symbols[symbol].triggered.connect(() => { if (root.servesApi) api.commandRan(symbol, true) })
        for (const dock of root.dockList) {
            dock.isOpenChanged.connect(() => { if (root.servesApi) api.panelChanged(root.panelName(dock)) })
            dock.isFloatingChanged.connect(() => { if (root.servesApi) api.panelChanged(root.panelName(dock)) })
        }
    }

    Component.onCompleted: {
        root.app.shellWindowLoaded(root)
        root.applyMenuShortcuts()
        root.connectApi()
        const turnedOn = root.app.settings.value("workspace.karaokeTurnedOn")
        root.karaokeTurnedOn = turnedOn.indexOf("audio.karaoke") >= 0
        root.foldTurnedOn = turnedOn.indexOf("grid.foldGenerated") >= 0
        if (root.karaokeTurnedOn || root.foldTurnedOn)
            root.workspaceLayout.resumeMode("Karaoke")
        if (root.app.gettingStartedDue())
            Qt.callLater(() => root.lazy(gettingStartedDialogLoader).openSetup())
        if (root.shellWindow === 0 && root.crashReports.pending.length > 0
                && !(root.crashReports.canSend && root.app.settings.value("privacy.alwaysSendCrashReports") === true))
            Qt.callLater(() => crashReportWindow.showReport("start"))
    }
    Autosaves {
        id: autosaves
        objectName: "autosaves"
        app: root.app
        settings: root.app.settings
        onBundlesDeleted: sessionWindows.offer.recountAutosaves()
    }

    function boundKeys(symbol, window) {
        return root.hotkeys.keys ? root.hotkeys.accelOf(symbol, window) : ""
    }
    function eitherKey(first, second) {
        return first.length && second.length ? qsTranslate("HikariSub.Audio", "%1 or %2").arg(first, second) : first + second
    }
    function mappedTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shift+click to set a shortcut")
    }
    function bitmapTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shift+click to set a shortcut")
    }
    // MUST KILL: kind is true or a string ("menu", "mapped", "macro", "bitmap"); two maps the second (id - 10) hotkey
    function hotkeyGesture(symbol, window, kind, two) {
        const mapped = kind === true || kind === "mapped"
        const held = mapped || kind === "bitmap"
        const shift = root.hotkeys.shiftClicked(!held)
        const second = !shift && mapped && two && root.hotkeys.ctrlClicked()
        if (!shift && !second) {
            if (root.keyRuns === 0)
                root.analytics.commandUsed(symbol, "pointer")
            return false
        }
        const target = root.hotkeys.gestureTarget(symbol, second)
        hotkeyMapping.capture(target.label ?? target.name, window ?? 0, !(mapped || kind === "macro"), (accel, type) => {
            const conflict = root.hotkeys.gestureConflict(target.id, accel, type)
            if (conflict.message === undefined)
                root.hotkeys.gestureMap(target.id, target.name, accel, type, "cancel")
            else
                hotkeyMapping.ask(conflict, answer => root.hotkeys.gestureMap(target.id, target.name, accel, type, answer))
        }, { command: target.id })
        return true
    }
    function gridGesture(item, symbol) {
        if (!root.hotkeyGesture(symbol, 1))
            return false
        if (item.checkable)
            item.toggle()
        return true
    }
    function runItem(item) {
        if (item.enabled)
            item.triggered()
    }
    function menuIndexOf(menu, item) {
        for (let i = 0; i < menu.count; ++i)
            if (menu.itemAt(i) === item)
                return i
        return menu.count - 1
    }
    function undoAiLine() {
        if (root.review.lineEditedSince() > 0) {
            reviewStrip.confirming = true
            root.focusPanel(editorPanel, Qt.OtherFocusReason)
            return
        }
        root.review.undoLine(false)
    }
    function undoAllAiChanges() {
        if (root.review.editedSince(0, -1) > 0) {
            root.showPanel(reviewDock)
            if (reviewLoader.item)
                reviewLoader.item.confirming = { change: 0, item: -1 }
            return
        }
        root.review.undo(0, -1, false)
    }
    Action {
        id: removeTagsAction
        text: qsTranslate("HikariSub.Tags", "Remove tags…")
        enabled: root.shell.hasEditingTarget
        onTriggered: if (!root.hotkeyGesture("GLOBAL_REMOVE_TAGS")) root.openRemoveTags()
    }
    function openSplitEvery() {
        splitEveryLoader.active = true
        splitEveryLoader.item.openDialog()
    }
    Loader {
        id: splitEveryLoader
        active: false
        sourceComponent: SplitEveryDialog {
            app: root.app
            settings: root.app.settings
            hasVideo: root.video.hasVideo
        }
    }
    function openRemoveTags() {
        removeTagsLoader.active = true
        removeTagsLoader.item.openDialog()
    }
    Loader {
        id: removeTagsLoader
        active: false
        sourceComponent: RemoveTagsDialog {
            app: root.app
            settings: root.app.settings
        }
    }
    Action {
        id: keepAiChangeAction
        text: qsTranslate("HikariSub.Review", "Keep AI change")
        enabled: root.review.on && root.review.current.count !== undefined
        onTriggered: if (!root.hotkeyGesture("GLOBAL_KEEP_AI_CHANGE")) root.review.keepLine()
    }
    Action {
        id: undoAiChangeAction
        text: qsTranslate("HikariSub.Review", "Undo AI change")
        enabled: root.review.on && root.review.current.count !== undefined
        onTriggered: if (!root.hotkeyGesture("GLOBAL_UNDO_AI_CHANGE")) root.undoAiLine()
    }
    Action {
        id: nextAiChangeAction
        text: qsTranslate("HikariSub.Review", "Next AI change")
        enabled: root.review.on && root.review.waiting > 0
        onTriggered: if (!root.hotkeyGesture("GLOBAL_NEXT_AI_CHANGE")) root.review.step(1)
    }
    Action {
        id: previousAiChangeAction
        text: qsTranslate("HikariSub.Review", "Previous AI change")
        enabled: root.review.on && root.review.waiting > 0
        onTriggered: if (!root.hotkeyGesture("GLOBAL_PREVIOUS_AI_CHANGE")) root.review.step(-1)
    }
    Action {
        id: keepAllAiChangesAction
        text: qsTranslate("HikariSub.Review", "Keep all")
        enabled: root.review.on && root.review.waiting > 0
        onTriggered: if (!root.hotkeyGesture("GLOBAL_KEEP_ALL_AI_CHANGES")) root.review.keep(0, -1)
    }
    Action {
        id: undoAllAiChangesAction
        text: qsTranslate("HikariSub.Review", "Undo all")
        enabled: root.review.on && root.review.waiting > 0
        onTriggered: if (!root.hotkeyGesture("GLOBAL_UNDO_ALL_AI_CHANGES")) root.undoAllAiChanges()
    }
    Action {
        id: aiActionsAction
        text: qsTranslate("HikariSub.Hotkeys", "AI actions…")
        enabled: root.inlineAi.on
        onTriggered: if (!root.hotkeyGesture("GLOBAL_AI_ACTIONS")) root.openInlineAiMenu()
    }
    Action {
        id: repeatAiActionAction
        text: qsTranslate("HikariSub.Hotkeys", "Repeat last AI action")
        enabled: root.inlineAi.on && !!root.inlineAi.ready.any && !!root.inlineAi.last.action
        onTriggered: {
            if (root.hotkeyGesture("GLOBAL_REPEAT_AI_ACTION"))
                return
            const inEditor = lineText.activeFocus
            root.repeatInlineAi(inEditor, inEditor ? lineText : null)
        }
    }
    Action {
        id: showAiAgentAction
        text: qsTranslate("HikariSub.Agent", "Agent panel")
        enabled: root.agent.on
        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_AI_AGENT")) root.showPanel(agentDock)
    }
    Action {
        id: newAiThreadAction
        text: qsTranslate("HikariSub.Agent", "New agent thread")
        enabled: root.agent.on
        onTriggered: {
            if (root.hotkeyGesture("GLOBAL_NEW_AI_THREAD"))
                return
            root.agent.newThread()
            root.showPanel(agentDock)
        }
    }
    Action {
        id: showAiReviewAction
        text: qsTranslate("HikariSub.Review", "Show panel")
        enabled: root.review.on
        onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_AI_REVIEW")) root.showPanel(reviewDock)
    }
    function globalActions() {
        return {
        GLOBAL_SAVE_SUBS: saveAction, GLOBAL_SAVE_ALL_SUBS: saveAllAction, GLOBAL_SAVE_SUBS_AS: saveAsAction,
        GLOBAL_EXPORT_SUBS_AS: exportAsAction,
        GLOBAL_SAVE_TRANSLATION: saveTranslationAction, GLOBAL_REMOVE_SUBS: removeSubsAction,
        GLOBAL_REDO: redoAction, GLOBAL_UNDO: undoAction, GLOBAL_UNDO_TO_LAST_SAVE: undoToLastSaveAction,
        GLOBAL_HISTORY: historyAction, GLOBAL_SEARCH: findAction, GLOBAL_FIND_REPLACE: findReplaceAction,
        GLOBAL_FIND_NEXT: findNextAction, GLOBAL_FIND_PREVIOUS: findPreviousAction,
        GLOBAL_OPEN_SELECT_LINES: selectLinesAction, GLOBAL_OPEN_AUDIO: openAudioAction,
        GLOBAL_AUDIO_FROM_VIDEO: audioFromVideoAction, GLOBAL_CLOSE_AUDIO: closeAudioAction,
        GLOBAL_RELOAD_AUDIO: reloadAudioAction, GLOBAL_RELOAD_VIDEO: reloadVideoAction,
        GLOBAL_AUTOMATION_LOAD_SCRIPT: loadScriptAction, GLOBAL_AUTOMATION_RELOAD_AUTOLOAD: reloadAutoloadAction,
        GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT: loadLastScriptAction,
        GLOBAL_AUTOMATION_REPEAT_LAST_RUN: repeatRunAction,
        GLOBAL_PLAY_PAUSE: playPauseAction,
        GLOBAL_PREVIOUS_FRAME: previousFrameAction, GLOBAL_NEXT_FRAME: nextFrameAction,
        GLOBAL_SET_VIDEO_AT_START_TIME: goToStartAction, GLOBAL_SET_VIDEO_AT_END_TIME: goToEndAction,
        GLOBAL_SET_START_TIME: setStartTimeAction, GLOBAL_SET_END_TIME: setEndTimeAction,
        GLOBAL_SHIFT_FRAMES_TO_VIDEO_START: shiftFramesToVideoStartAction,
        GLOBAL_SHIFT_FRAMES_TO_VIDEO_END: shiftFramesToVideoEndAction,
        GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_START: shiftFollowingFramesToVideoStartAction,
        GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_END: shiftFollowingFramesToVideoEndAction,
        GLOBAL_JOIN_PREVIOUS_AUDIO_SELECTION: joinPreviousAudioSelectionAction,
        GLOBAL_CENTER_TIMES_IN_FRAMES: centerTimesInFramesAction,
        GLOBAL_ALIGN_TO_VIDEO: alignToVideoAction,
        GLOBAL_TRACK_COLOUR: trackColourAction,
        GLOBAL_FIND_FADE: findFadeAction,
        GLOBAL_PERSPECTIVE_TRACKING: perspectiveTrackingAction,
        GLOBAL_APPLY_MOTION_DATA: applyMotionDataAction,
        GLOBAL_BLUR_AND_GLOW_LAYERS: blurAndGlowLayersAction,
        GLOBAL_REVERT_MOTION_DATA: revertMotionDataAction,
        GLOBAL_COPY_MOTION_DATA: copyMotionDataAction,
        GLOBAL_COPY_POWER_PIN_TRACK: copyPowerPinTrackAction,
        GLOBAL_EXTEND_TRACKED_LINES: extendTrackedLinesAction,
        GLOBAL_EXTRAPOLATE_MOVE: extrapolateMoveAction,
        GLOBAL_REMOVE_TAGS: removeTagsAction,
        GLOBAL_GO_TO_NEXT_KEYFRAME: nextKeyframeAction, GLOBAL_GO_TO_PREVIOUS_KEYFRAME: previousKeyframeAction,
        GLOBAL_SET_AUDIO_FROM_VIDEO: setAudioFromVideoAction, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: setAudioMarkFromVideoAction,
        GLOBAL_VIDEO_ZOOM: videoZoomAction, GLOBAL_RESET_VIDEO_ZOOM: resetVideoZoomAction,
        GLOBAL_VIDEO_RENDER_COST: renderCostAction,
        GLOBAL_OPEN_SUBS: openAction, GLOBAL_OPEN_VIDEO: openVideoAction, GLOBAL_OPEN_KEYFRAMES: openKeyframesAction,
        GLOBAL_OPEN_FOLDER: openFolderAction, GLOBAL_CLOSE_PROJECT: closeProjectAction,
        GLOBAL_OPEN_TIMECODES: openTimecodesAction, GLOBAL_SAVE_TIMECODES: saveTimecodesAction,
        GLOBAL_CLOSE_TIMECODES: closeTimecodesAction,
        GLOBAL_OPEN_AUTO_SAVE: openAutoSaveAction,
        GLOBAL_OPEN_DUMMY_VIDEO: dummyVideoAction,
        GLOBAL_DELETE_TEMPORARY_FILES: removeTemporaryAction, GLOBAL_SETTINGS: settingsAction,
        GLOBAL_ABOUT: aboutAction, GLOBAL_HELPERS: creditsAction, GLOBAL_HELP: userGuideAction,
        GLOBAL_GETTING_STARTED: gettingStartedAction,
        GLOBAL_PLUGINS: pluginsAction, GLOBAL_TOGGLE_LOCK_LAYOUT: lockLayoutAction,
        GLOBAL_VIDEO_DETAILS: videoDetailsAction,
        GLOBAL_ANSI: reportIssueAction, GLOBAL_CHECK_FOR_UPDATES: checkForUpdatesAction,
        GLOBAL_EDITOR_HIDE_TAGS: hideEditorTagsAction,
        GLOBAL_TRANSLATION_MODE: translationModeAction,
        GLOBAL_NODE_EFFECTS: nodeEffectsAction,
        GLOBAL_INSERT_HARD_SPACE: insertHardSpaceAction,
        GLOBAL_KEEP_AI_CHANGE: keepAiChangeAction, GLOBAL_UNDO_AI_CHANGE: undoAiChangeAction,
        GLOBAL_NEXT_AI_CHANGE: nextAiChangeAction, GLOBAL_PREVIOUS_AI_CHANGE: previousAiChangeAction,
        GLOBAL_KEEP_ALL_AI_CHANGES: keepAllAiChangesAction, GLOBAL_UNDO_ALL_AI_CHANGES: undoAllAiChangesAction,
        GLOBAL_SHOW_AI_REVIEW: showAiReviewAction,
        GLOBAL_SHOW_AI_AGENT: showAiAgentAction, GLOBAL_NEW_AI_THREAD: newAiThreadAction,
        GLOBAL_AI_ACTIONS: aiActionsAction, GLOBAL_REPEAT_AI_ACTION: repeatAiActionAction,
        GLOBAL_CLOSE_PAGE: closeAction
    }
    }
    function globalItems() {
        return {
        GLOBAL_OPEN_SPELLCHECKER: checkSpellingItem, GLOBAL_OPEN_ASS_PROPERTIES: assPropertiesItem,
        GLOBAL_OPEN_STYLE_MANAGER: styleManagerItem, GLOBAL_OPEN_SUBS_RESAMPLE: resampleItem,
        GLOBAL_OPEN_FONT_COLLECTOR: fontCollectorItem,
        GLOBAL_SHOW_SHIFT_TIMES: showShiftTimesItem, GLOBAL_SHOW_TIMING_POSTPROCESSOR: showPostprocessorItem,
        GLOBAL_SHOW_QC_NOTES: showQcNotesItem,
        GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME: snapStartToPreviousKeyframeItem,
        GLOBAL_SNAP_START_TO_NEXT_KEYFRAME: snapStartToNextKeyframeItem,
        GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME: snapEndToPreviousKeyframeItem,
        GLOBAL_SNAP_END_TO_NEXT_KEYFRAME: snapEndToNextKeyframeItem,
        GLOBAL_ADD_LEAD_IN: addLeadInItem,
        GLOBAL_REMOVE_LEAD_IN: removeLeadInItem,
        GLOBAL_ADD_LEAD_OUT: addLeadOutItem,
        GLOBAL_REMOVE_LEAD_OUT: removeLeadOutItem,
        GLOBAL_START_FRAME_EARLIER: startFrameEarlierItem,
        GLOBAL_START_FRAME_LATER: startFrameLaterItem,
        GLOBAL_END_FRAME_EARLIER: endFrameEarlierItem,
        GLOBAL_END_FRAME_LATER: endFrameLaterItem,
        GLOBAL_EXTEND_PREVIOUS_TO_START: extendPreviousToStartItem,
        GLOBAL_EXTEND_NEXT_TO_END: extendNextToEndItem,
        GLOBAL_TIMING_ASSISTANT: timingAssistantItem,
        GLOBAL_TIMING_ASSISTANT_ACCEPT: acceptTimingSuggestionItem,
        GLOBAL_LOAD_EXTERNAL_SESSION: loadSessionFileItem, GLOBAL_SAVE_EXTERNAL_SESSION: saveSessionFileItem,
        GLOBAL_LOAD_LAST_SESSION: loadLastSessionItem,
        GLOBAL_CHECK_KARAOKE: checkKaraokeItem,
        GLOBAL_KANJI_TIMER: kanjiTimerItem,
    }
    }
    function commandMenus() {
        const symbolOf = new Map()
        const actions = root.globalActions(), items = root.globalItems()
        for (const s in actions)
            symbolOf.set(actions[s], s)
        for (const s in items)
            symbolOf.set(items[s], s)
        const plain = text => text.replace(/&(&?)/g, "$1")
        const out = {}
        const walk = (menu, path) => {
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i)
                if (!item)
                    continue
                if (item.subMenu) {
                    walk(item.subMenu, path.concat(plain(item.subMenu.title)))
                    continue
                }
                const symbol = item.command || symbolOf.get(item.action) || symbolOf.get(item)
                if (!symbol)
                    continue
                const place = path.concat(plain(item.docsText || item.text)).join(" > ")
                if (!out[symbol])
                    out[symbol] = []
                if (!out[symbol].includes(place))
                    out[symbol].push(place)
            }
        }
        for (let i = 0; i < appMenus.count; ++i)
            walk(appMenus.menuAt(i), [plain(appMenus.menuAt(i).title)])
        walk(gridMenu, ["Grid menu"])
        walk(gridHeaderMenu, ["Grid header menu"])
        walk(gridFilteringMenu, ["Grid options", plain(gridFilteringMenu.title)])
        walk(gridColumnsMenu, ["Grid options", plain(gridColumnsMenu.title)])
        for (const own of root.panelOwnItems(gridDock))
            if (own.command) {
                const place = "Grid options > " + own.text
                out[own.command] = (out[own.command] ?? []).concat(place)
            }
        return out
    }
    function applyMenuShortcuts() {
        const symbolOf = new Map()
        const actions = root.globalActions(), items = root.globalItems()
        for (const s in actions)
            symbolOf.set(actions[s], s)
        for (const s in items)
            symbolOf.set(items[s], s)
        const walk = menu => {
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i)
                if (!item)
                    continue
                if (item.subMenu)
                    walk(item.subMenu)
                else if (item.shortcutText !== undefined) {
                    const symbol = symbolOf.get(item.action) ?? symbolOf.get(item)
                    item.shortcutText = symbol ? root.boundKeys(symbol, 0) : ""
                }
            }
        }
        for (let i = 0; i < appMenus.count; ++i)
            walk(appMenus.menuAt(i))
    }
    Connections {
        target: root.hotkeys
        function onInstalledChanged() { root.applyMenuShortcuts() }
    }
    function runGlobalHotkey(symbol) {
        return root.hotkeys.report(symbol, root.hotkeys.rehearsing() || root.byKey(() => root.globalHotkey(symbol)))
    }
    function runEditorHotkey(action, field) {
        return root.hotkeys.report(action, root.hotkeys.rehearsing() || root.byKey(() => root.editorHotkey(action, field)))
    }
    function runVideoHotkey(action) {
        return root.hotkeys.report(action, root.hotkeys.rehearsing() || root.byKey(() => root.videoHotkey(action)))
    }
    function runGridHotkey(action) {
        return root.hotkeys.report(action, root.hotkeys.rehearsing() || root.byKey(() => root.gridHotkey(action)))
    }
    function runAudioHotkey(action) {
        return root.hotkeys.report(action, root.hotkeys.rehearsing() || root.byKey(() => root.audioHotkey(action)))
    }
    property int keyRuns: 0
    function byKey(run) {
        ++root.keyRuns
        try {
            return run()
        } finally {
            --root.keyRuns
        }
    }
    function globalHotkey(symbol) {
        const field = root.editedField()
        if (symbol.startsWith("AUDIO_"))
            return root.audio.loaded && root.runAudioHotkey(symbol)
        if (symbol.startsWith("VIDEO_"))
            return root.runVideoHotkey(symbol)
        if (symbol.startsWith("EDITBOX_"))
            return root.runEditorHotkey(symbol, field)
        if (symbol.startsWith("GRID_"))
            return root.runGridHotkey(symbol)
        const menuSelected1 = ["GLOBAL_OPEN_SUBS", "GLOBAL_OPEN_VIDEO", "GLOBAL_OPEN_KEYFRAMES", "GLOBAL_OPEN_DUMMY_VIDEO",
                               "GLOBAL_OPEN_TIMECODES", "GLOBAL_SAVE_TIMECODES",
                               "GLOBAL_OPEN_AUTO_SAVE", "GLOBAL_DELETE_TEMPORARY_FILES",
                               "GLOBAL_SETTINGS", "GLOBAL_QUIT", "GLOBAL_ABOUT", "GLOBAL_HELPERS",
                               "GLOBAL_HELP", "GLOBAL_ANSI", "GLOBAL_CHECK_FOR_UPDATES", "GLOBAL_SELECT_FROM_VIDEO",
                               "GLOBAL_PLAY_ACTUAL_LINE", "GLOBAL_STYLE_MANAGER_CLEAN_STYLE"]
        const actions = root.globalActions()
        const items = root.globalItems()
        if (menuSelected1.includes(symbol) && root.hotkeys.repeatedKey(symbol))
            return true
        if (actions[symbol] !== undefined) {
            actions[symbol].trigger()
            return true
        }
        if (items[symbol] !== undefined) {
            root.runItem(items[symbol])
            return true
        }
        const sort = root.sortKeys.find(k => k.all === symbol || k.selected === symbol)
        if (sort) {
            if (root.editor.editable)
                root.app.sortLines(sort.key, sort.selected === symbol)
            return true
        }
        const editing = root.shell.hasEditingTarget
        switch (symbol) {
        case "GLOBAL_QUIT": root.beginClose("quit"); return true
        case "GLOBAL_CLOSE_WINDOW": Qt.callLater(root.close); return true
        case "GLOBAL_IMPORT_LYRICS": lyricsDialog.show(); return true
        case "GLOBAL_IMPORT_LEGACY_SETTINGS": root.importLegacySettings(); return true
        case "GLOBAL_CONVERT_FOLDS_TO_GROUPS": if (editing) root.app.convertFoldsToGroups(); return true
        case "GLOBAL_INTERPOLATE_LINES": if (editing) root.openInterpolateLines(); return true
        case "GLOBAL_PLAY_ACTUAL_LINE":
            if (root.video.hasVideo) {
                root.focusPanel(lineText, Qt.OtherFocusReason)
                root.video.playActualLine()
            }
            return true
        case "GLOBAL_STYLE_MANAGER_CLEAN_STYLE":
            if (editing)
                root.lazy(styleManagerWindowLoader).deleteUnused()
            return true
        case "GLOBAL_SHIFT_TIMES": if (editing) root.applyShift("shift"); return true
        case "GLOBAL_TIMING_POSTPROCESSOR": if (editing) root.applyShift("postprocessor"); return true
        case "GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT": if (editing) root.repeatBlurAndGlowLayers(); return true
        case "GLOBAL_NEXT_QC_NOTE": if (editing) root.stepQcNote(true); return true
        case "GLOBAL_PREVIOUS_QC_NOTE": if (editing) root.stepQcNote(false); return true
        case "GLOBAL_ADD_QC_NOTE": if (editing) root.addQcNote(-1); return true
        case "GLOBAL_JOIN_WITH_PREVIOUS": if (editing) root.app.joinLines("previous"); return true
        case "GLOBAL_JOIN_WITH_NEXT": if (editing) root.app.joinLines("next"); return true
        case "GLOBAL_MOVE_TAB_TO_NEW_WINDOW":
            if (root.app.editingDocument() > 0)
                root.app.moveTabToNewWindow(root.app.editingDocument(), Qt.point(0, 0), false)
            return true
        case "GLOBAL_MOVE_TAB_TO_WINDOW": documentTabBar.openMoveToWindowMenu(); return true
        case "GLOBAL_NEXT_TAB": root.app.changeTab(1); return true
        case "GLOBAL_PREVIOUS_TAB": root.app.changeTab(-1); return true
        case "GLOBAL_REMOVE_LINES": if (editing) root.app.deleteLines(); return true
        case "GLOBAL_HIDE_TAGS": root.app.toggleHideTags(); return true
        case "GLOBAL_PREVIOUS_LINE": if (editing) root.editor.nextLine(-1); return true
        case "GLOBAL_NEXT_LINE": if (editing) root.editor.nextLine(1); return true
        case "GLOBAL_REMOVE_TEXT": if (editing) root.app.deleteText(); return true
        case "GLOBAL_TEXT_INSERT_BALANCED_BREAK": if (editing) root.app.textCommand("balancedBreak"); return true
        case "GLOBAL_TEXT_MOVE_BREAK_LEFT": if (editing) root.app.textCommand("breakLeft"); return true
        case "GLOBAL_TEXT_MOVE_BREAK_RIGHT": if (editing) root.app.textCommand("breakRight"); return true
        case "GLOBAL_TEXT_REMOVE_BREAKS": if (editing) root.app.textCommand("removeBreaks"); return true
        case "GLOBAL_TEXT_WRAP_AT_BREAKS_ONLY": if (editing) root.app.textCommand("wrapAtBreaksOnly"); return true
        case "GLOBAL_TEXT_LOWERCASE": if (editing) root.app.textCommand("lowercase"); return true
        case "GLOBAL_TEXT_UPPERCASE": if (editing) root.app.textCommand("uppercase"); return true
        case "GLOBAL_TEXT_TITLE_CASE": if (editing) root.app.textCommand("titleCase"); return true
        case "GLOBAL_TEXT_SENTENCE_CASE": if (editing) root.app.textCommand("sentenceCase"); return true
        case "GLOBAL_TEXT_FULL_WIDTH": if (editing) root.app.textCommand("fullWidth"); return true
        case "GLOBAL_TEXT_HALF_WIDTH": if (editing) root.app.textCommand("halfWidth"); return true
        case "GLOBAL_SWAP_DIALOGUE_ALTERNATIVES": if (editing) root.app.textCommand("swapAlternatives"); return true
        case "GLOBAL_ADD_PAGE": root.app.addPage(); return true
        case "GLOBAL_SELECT_FROM_VIDEO": root.app.selectLineFromVideo(); return true
        case "GLOBAL_SNAP_WITH_START": root.app.snapToKeyframe(true); return true
        case "GLOBAL_SNAP_WITH_END": root.app.snapToKeyframe(false); return true
        }
        return false
    }
    function editorHotkey(action, field) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action)
        const tags = { EDITBOX_INSERT_BOLD: "b", EDITBOX_INSERT_ITALIC: "i", EDITBOX_CHANGE_UNDERLINE: "u",
                       EDITBOX_CHANGE_STRIKEOUT: "s" }
        const colours = { EDITBOX_CHANGE_COLOR_PRIMARY: 1, EDITBOX_CHANGE_COLOR_SECONDARY: 2,
                          EDITBOX_CHANGE_COLOR_OUTLINE: 3, EDITBOX_CHANGE_COLOR_SHADOW: 4 }
        const edited = root.editor.translationMode ? translationText : lineText
        if (tags[action] !== undefined) {
            root.editor.toggleTagIn(field.role, tags[action], field.selectionStart, field.selectionEnd)
            return true
        }
        if (action.startsWith("EDITBOX_PLACE_RUN_") || action === "EDITBOX_PLACE_NEXT_RUN") {
            const number = action === "EDITBOX_PLACE_NEXT_RUN" ? 0 : Number(action.substring(18))
            if (field.role === 1)
                root.editor.placeRun(number, field.selectionStart, field.selectionEnd)
            return true
        }
        if (colours[action] !== undefined) {
            root.colourClick(colours[action], true, field)
            return true
        }
        if (action.startsWith("EDITBOX_TAG_BUTTON")) {
            const index = Number(action.substring(18)) - 1
            if (index < root.tagButtons.buttons.length)
                root.applyTagButton(index)
            return true
        }
        switch (action) {
        case "EDITBOX_COMMIT_GO_NEXT_LINE": root.app.commitAndAdvance(); return true
        case "EDITBOX_COMMIT": root.editor.applyChanges(); return true
        case "EDITBOX_SPLIT_LINE": root.editor.splitLine(field.role, field.selectionStart, field.selectionEnd); return true
        case "EDITBOX_SET_DOUBTFUL": root.editor.toggleUnconfirmedAndAdvance(); return true
        case "EDITBOX_START_DIFFERENCE":
        case "EDITBOX_END_DIFFERENCE":
            root.editor.insertTimeDifference(action === "EDITBOX_END_DIFFERENCE", edited.selectionStart, edited.selectionEnd)
            return true
        case "EDITBOX_FIND_NEXT_DOUBTFUL": root.editor.findNextUnconfirmed(); return true
        case "EDITBOX_FIND_NEXT_UNTRANSLATED": root.editor.findNextUntranslated(); return true
        case "EDITBOX_CHANGE_FONT": fontPicker.openFor(field.role, field.selectionStart, field.selectionEnd); return true
        case "EDITBOX_PASTE_ALL_TO_TRANSLATION": root.editor.pasteAllToTranslation(); return true
        case "EDITBOX_PASTE_SELECTION_TO_TRANSLATION":
            root.editor.pasteSelectionToTranslation(lineText.selectionStart, lineText.selectionEnd, translationText.cursorPosition)
            return true
        case "EDITBOX_HIDE_ORIGINAL": root.editor.commentOutOriginal(); return true
        }
        return action.startsWith("VIDEO_") && root.runVideoHotkey(action)
    }
    function videoHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action)
        if (root.hotkeys.repeatedKey(action, 50))
            return true
        if (root.visualTools.runHotkey(action))
            return true
        switch (action) {
        case "VIDEO_PLAY_PAUSE": root.video.togglePlay(); return true
        case "VIDEO_5_SECONDS_FORWARD": root.video.seekBy(5000); return true
        case "VIDEO_5_SECONDS_BACKWARD": root.video.seekBy(-5000); return true
        case "VIDEO_MINUTE_FORWARD": root.video.seekBy(60000); return true
        case "VIDEO_MINUTE_BACKWARD": root.video.seekBy(-60000); return true
        case "VIDEO_STOP": root.video.stop(); return true
        case "VIDEO_COPY_COORDS": root.visualTools.copyCoordinatesAtCursor(visualOverlay); return true
        case "VIDEO_PREVIOUS_FILE": videoFileQuestion.ask(false); return true
        case "VIDEO_NEXT_FILE": videoFileQuestion.ask(true); return true
        case "VIDEO_PREVIOUS_CHAPTER": root.video.previousChapter(); return true
        case "VIDEO_NEXT_CHAPTER": root.video.nextChapter(); return true
        case "VIDEO_DELETE_FILE": root.video.unloadVideo(); return true
        case "VIDEO_VOLUME_PLUS": root.videoView.stepVolume(true); return true
        case "VIDEO_VOLUME_MINUS": root.videoView.stepVolume(false); return true
        case "VIDEO_HIDE_PROGRESS_BAR": root.videoView.toggleProgressBar(); return true
        case "VIDEO_FULL_SCREEN": root.videoFullscreen.toggle(0); return true
        case "VIDEO_FULLSCREEN_BAR": root.videoFullscreen.cycleBarMode(); return true
        case "VIDEO_ASPECT_RATIO": root.lazy(aspectRatioDialogLoader).openWindow(root.videoFullscreen.active ? videoFullscreenWindow : null); return true
        case "VIDEO_SAVE_FRAME_TO_PNG":
        case "VIDEO_COPY_FRAME_TO_CLIPBOARD":
        case "VIDEO_SAVE_SUBBED_FRAME_TO_PNG":
        case "VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD":
        case "VIDEO_SAVE_SUBTITLES_TO_PNG":
        case "VIDEO_COPY_SUBTITLES_TO_CLIPBOARD":
            root.videoView.snapshot(action)
            return true
        }
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, root.editedField())
        return action.startsWith("GRID_") && root.runGridHotkey(action)
    }
    function gridHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action)
        if (action.startsWith("AUDIO_"))
            return !root.hotkeys.repeatedKey(action) && root.audio.runHotkey(action)
        if (root.hotkeys.repeatedKey(action))
            return true
        const columns = { GRID_HIDE_LAYER: 1, GRID_HIDE_START: 2, GRID_HIDE_END: 4, GRID_HIDE_STYLE: 8,
                          GRID_HIDE_ACTOR: 16, GRID_HIDE_MARGINL: 32, GRID_HIDE_MARGINR: 64, GRID_HIDE_MARGINV: 128,
                          GRID_HIDE_EFFECT: 256, GRID_HIDE_CPS: 512, GRID_HIDE_WRAPS: 8192 }
        const filterBits = { GRID_FILTER_BY_STYLES: 1, GRID_FILTER_BY_SELECTIONS: 2, GRID_FILTER_BY_DIALOGUES: 4,
                             GRID_FILTER_BY_DOUBTFUL: 8, GRID_FILTER_BY_UNTRANSLATED: 16 }
        if (columns[action] !== undefined) {
            root.shell.toggleColumn(columns[action])
            return true
        }
        if (filterBits[action] !== undefined) {
            root.gridFilter.setFilterBy(filterBits[action], (root.gridFilter.filterBy & filterBits[action]) === 0)
            return true
        }
        const video = root.video.hasVideo
        switch (action) {
        case "GRID_DUPLICATE_LINES": root.app.duplicateLines(); return true
        case "GRID_INSERT_BEFORE": root.app.insertLine(true); return true
        case "GRID_INSERT_AFTER": root.app.insertLine(false); return true
        case "GRID_INSERT_BEFORE_VIDEO": if (video) root.app.insertLine(true, "video"); return true
        case "GRID_INSERT_AFTER_VIDEO": if (video) root.app.insertLine(false, "video"); return true
        case "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME": if (video) root.app.insertLine(true, "frame"); return true
        case "GRID_INSERT_AFTER_WITH_VIDEO_FRAME": if (video) root.app.insertLine(false, "frame"); return true
        case "GRID_SWAP_LINES": root.app.swapLines(); return true
        case "GRID_JOIN_LINES": root.app.joinLines("join"); return true
        case "GRID_JOIN_TO_FIRST_LINE": root.app.joinLines("first"); return true
        case "GRID_JOIN_TO_LAST_LINE": root.app.joinLines("last"); return true
        case "GRID_MAKE_CONTINOUS_PREVIOUS_LINE": root.app.makeContinuous(true); return true
        case "GRID_MAKE_CONTINOUS_NEXT_LINE": root.app.makeContinuous(false); return true
        case "GRID_SPLIT_BY_VIDEO_TIME": if (video) root.app.splitLines("videoTime"); return true
        case "GRID_SPLIT_BY_FRAME": if (video) root.app.splitLines("frames"); return true
        case "GRID_SPLIT_BY_CHARS": root.app.splitLines("chars"); return true
        case "GRID_SPLIT_BY_WORDS": root.app.splitLines("words"); return true
        case "GRID_SPLIT_BY_WRAPS": root.app.splitLines("wraps"); return true
        case "GRID_SPLIT_AT_TAG_BLOCKS": root.app.splitLines("tagBlocks"); return true
        case "GRID_SPLIT_EVERY_N": root.openSplitEvery(); return true
        case "GRID_TREE_MAKE": root.app.makeGroups(); return true
        case "GRID_SELECT_VISIBLE_LINES": root.app.selectLinesVisibleOnVideo(); return true
        case "GRID_SHOW_PREVIEW": if (root.app.canShowPreview()) root.app.showPreview(); return true
        case "GRID_HIDE_SELECTED": root.app.hideSelectedLines(); return true
        case "GRID_FILTER": root.app.filterLines(); return true
        case "GRID_FILTER_BY_NOTHING": root.app.turnOffFiltering(); return true
        case "GRID_SUBS_FROM_MKV": matroskaSubtitles.begin(); return true
        case "GRID_FILTER_INVERT": root.gridFilter.inverted = !root.gridFilter.inverted; return true
        case "GRID_FILTER_DO_NOT_RESET": root.gridFilter.addToFilter = !root.gridFilter.addToFilter; return true
        case "GRID_FILTER_AFTER_SUBS_LOAD": root.gridFilter.afterLoad = !root.gridFilter.afterLoad; return true
        case "GRID_FILTER_IGNORE_IN_ACTIONS":
            root.gridFilter.ignoreInActions = root.hotkeys.ignoreFilteringFromOption()
            return true
        case "GRID_COPY_COLUMNS": root.chooseColumns(false); return true
        case "GRID_PASTE_COLUMNS": root.chooseColumns(true); return true
        case "GRID_SET_NEW_FPS": root.openSetNewFps(); return true
        case "GRID_SET_FPS_FROM_VIDEO": if (video) root.app.setFpsFromVideo(); return true
        case "GRID_PASTE_TRANSLATION":
            if (root.app.canPasteTranslation()) {
                translationFileDialog.currentFolder = root.app.targetFolder()
                translationFileDialog.open()
            }
            return true
        case "GRID_TRANSLATION_DIALOG": if (root.app.canShiftTranslation()) root.lazy(translationShiftWindowLoader).open(); return true
        }
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, root.editedField())
        return action.startsWith("VIDEO_") && root.runVideoHotkey(action)
    }
    function audioHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action)
        if (action.startsWith("AUDIO_"))
            return !root.hotkeys.repeatedKey(action) && root.audio.runHotkey(action)
        if (action.startsWith("VIDEO_"))
            return root.runVideoHotkey(action)
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, root.editedField())
        return action.startsWith("GRID_") && root.runGridHotkey(action)
    }
    HotkeyCapture {
        id: hotkeyMapping
        objectName: "hotkeyMapping"
        hotkeys: root.hotkeys
        settings: root.app.settings
    }
    Repeater {
        model: root.automationHotkeys.shortcuts
        delegate: Item {
            required property var modelData
            Shortcut {
                sequence: modelData.keys
                context: Qt.ApplicationShortcut
                enabled: root.visible && !root.menuHasKeys && !root.workspaceLayout.modalOpen
                         && !root.hotkeys.macroTakenSequences.includes(modelData.keys)
                onActivated: root.automationHotkeys.run(modelData.legacyName)
            }
        }
    }
}
