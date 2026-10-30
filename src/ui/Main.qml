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
    title: shell.hasEditingTarget ? qsTr("%1 - HikariSub").arg(shell.editingTitle) : "HikariSub"
    color: Theme.background

    required property ShellController shell
    required property LineEditorController editor
    required property VideoController video
    required property AudioController audio
    required property var app
    required property var automation
    required property AutomationManagerController automationManager
    required property TemplateEditorController templateEditor
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
    required property var crashReports
    required property var styleManager
    required property var kanjiTimer
    required property var fontCollector
    required property var fontCatalogs
    required property var matroska
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

    readonly property var conversionItems: [
        ["ass", qsTr("Convert to ASS"), "GLOBAL_CONVERT_TO_ASS", qsTr("Converts to ASS format")],
        ["srt", qsTr("Convert to SRT"), "GLOBAL_CONVERT_TO_SRT", qsTr("Converts to SRT format")],
        ["mdvd", qsTr("Convert to MDVD"), "GLOBAL_CONVERT_TO_MDVD", qsTr("Converts to microDVD format")],
        ["mpl2", qsTr("Convert to MPL2"), "GLOBAL_CONVERT_TO_MPL2", qsTr("Converts to MPL2 format")],
        ["tmp", qsTr("Convert to TMP"), "GLOBAL_CONVERT_TO_TMP", qsTr("Converts to TMPlayer format (not recommended)")]]

    readonly property var sortKeys: [
        { key: "start", label: qsTr("The starting time"), all: "GLOBAL_SORT_ALL_BY_START_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_START_TIMES", help: qsTr("Sort by start time") },
        { key: "end", label: qsTr("End time"), all: "GLOBAL_SORT_ALL_BY_END_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_END_TIMES", help: qsTr("Sort by end time") },
        { key: "style", label: qsTr("Styles"), all: "GLOBAL_SORT_ALL_BY_STYLE", selected: "GLOBAL_SORT_SELECTED_BY_STYLE",
          help: qsTr("Sort by styles") },
        { key: "actor", label: qsTr("Actor"), all: "GLOBAL_SORT_ALL_BY_ACTOR", selected: "GLOBAL_SORT_SELECTED_BY_ACTOR",
          help: qsTr("Sort by actor") },
        { key: "effect", label: qsTr("Effect"), all: "GLOBAL_SORT_ALL_BY_EFFECT", selected: "GLOBAL_SORT_SELECTED_BY_EFFECT",
          help: qsTr("Sort by effect") },
        { key: "layer", label: qsTr("Layer"), all: "GLOBAL_SORT_ALL_BY_LAYER", selected: "GLOBAL_SORT_SELECTED_BY_LAYER",
          help: qsTr("Sort by layer") }
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
    function admitClose() {
        if (!root.app.quitApproved) {
            const rows = root.app.reviewClose("quit")
            if (rows.length > 0) {
                closeReview.review(rows)
                return false
            }
        }
        root.workspaceLayout.saveForQuit()
        return true
    }
    onClosing: close => {
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
                if (root.app.quitApproved)
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
        Docking.setMinimumSize("Editor", Qt.size(editorPanel.minimumSize.width,
                                                 Math.ceil(editorColumn.implicitHeight) + 8))
        for (let pass = 0; pass < 3; ++pass) {
            const body = root.workspaceLayout.panelSize(audioDock).height - chrome
            // MUST KILL: 170 is a magic audio box body height
            const deficit = Math.round(170 - body)
            if (deficit === 0 || !audioDock.isOpen)
                break
            Docking.resizeInLayout("Audio", 0, 0, 0, deficit)
        }
        editorPanel.reportMinimumSize()
    }

    function defaultLayout() {
        dockingArea.addDockWidget(searchDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 280))
        searchDock.close()
        dockingArea.addDockWidget(gridDock, KDDW.KDDockWidgets.Location_OnBottom)
        dockingArea.addDockWidget(videoDock, KDDW.KDDockWidgets.Location_OnTop, gridDock, Qt.size(640, 520))
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnRight, videoDock)
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 272))
        dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        originalDock.close()
        dockingArea.addDockWidget(referenceDock, KDDW.KDDockWidgets.Location_OnBottom, gridDock, Qt.size(0, 160))
        if (!root.shell.hasReference)
            referenceDock.close()
        editorDock.addDockWidgetAsTab(timingDock)
        editorDock.addDockWidgetAsTab(automationDock)
        editorDock.addDockWidgetAsTab(stylesDock)
        editorDock.addDockWidgetAsTab(templateDock)
        editorDock.setAsCurrentTab()
        timingDock.close()
        automationDock.close()
        stylesDock.close()
        templateDock.close()
        dockingArea.addDockWidget(misspellDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(420, 0))
        misspellDock.close()
    }

    function typesettingLayout() {
        const area = root.workspaceLayout.panelSize(gridDock).width
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnLeft, gridDock,
                                  Qt.size(Math.round(area * 0.44), 0))
        const fieldsShort = Math.ceil(editorInspector.wideMinimum - editorColumn.width)
        const room = Math.round(area * 0.5) - root.workspaceLayout.panelSize(editorDock).width
        if (fieldsShort > 0 && room > 0)
            Docking.resizeInLayout("Editor", 0, 0, Math.min(fieldsShort, room), 0)
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
            Docking.resizeInLayout("Styles", short, 0, 0, 0)
        // KDDW doesn't move the closed Audio/Original places' edge, so the Video panel's bottom edge moves instead.
        editorColumn.ensurePolished()
        const lower = root.workspaceLayout.panelSize(editorDock).height
        const natural = Math.ceil(editorColumn.implicitHeight) + 8 + lower - editorPanel.height
        if (lower !== natural)
            Docking.resizeInLayout("Video", 0, 0, 0, lower - natural)
    }
    function karaokeLayout() {
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, gridDock,
                                  Qt.size(0, root.workspaceLayout.panelSize(audioDock).height))
        templateDock.open()
        templateDock.raise()
        const lower = root.workspaceLayout.panelSize(gridDock).height
        const short = Math.ceil(lower - grid.height + grid.rowHeight * 8)
        if (short !== lower)
            Docking.resizeInLayout("Grid", 0, short - lower, 0, 0)
    }
    Connections {
        target: root.workspaceLayout
        function onBuildArrangement(name) {
            if (name === "Typesetting")
                root.typesettingLayout()
            else if (name === "Karaoke")
                root.karaokeLayout()
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
        if (Docking.hasPlace("Original"))
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

    function openAutomationRun(runId) {
        if (root.videoFullscreen.active)
            root.videoFullscreen.leave()
        root.showPanel(automationDock)
        automationTool.showRun(runId)
    }

    readonly property list<Item> panels: [videoPanel, audioPanel, originalPanel, editorPanel, gridPanel, referencePanel,
                                          timingPanel, searchPanel, automationPanel, spellPanel, logPanel, stylesPanel,
                                          misspellPanel, templatePanel]
    readonly property var dockList: [videoDock, audioDock, originalDock, editorDock, gridDock, referenceDock, timingDock,
                                     searchDock, automationDock, spellDock, logDock, stylesDock, misspellDock, templateDock]
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

    function applyPreset(name) {
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applyPreset(name))
        root.analytics.layoutChanged(name)
    }
    function applySavedLayout(name) {
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applySavedLayout(name))
        root.analytics.layoutChanged("saved")
    }
    function resetLayout() {
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

    // MUST KILL: which is a magic int search tab (0 Search, 1 Find and replace)
    function openSearch(which) {
        if (searchDock.isOpen && searchPanel.activeFocus && searchTool.tab === which) {
            searchDock.close()
            root.focusPanel(editorPanel, Qt.OtherFocusReason)
            return
        }
        searchTool.showTab(which)
        root.showPanel(searchDock)
        searchTool.focusFind()
    }

    function runShiftTimes() {
        const which = root.shiftTimes.settings.whichTimes
        const pp = root.shiftTimes.settings.postprocessor
        // MUST KILL: whichTimes (1 start, else end) and the postprocessor bit test are magic ints
        if (which !== 0 && !(pp >= 16 && (pp & 15) !== 0)) {
            const start = which === 1
            const target = root.app.editingTargetId()
            shiftQuestion.askedFor = target
            shiftQuestion.askWith({
                title: qsTranslate("HikariSub.TimeShift", "Shift times"),
                text: start ? qsTranslate("HikariSub.TimeShift", "Shift only the start times?")
                            : qsTranslate("HikariSub.TimeShift", "Shift only the end times?"),
                detail: start ? qsTranslate("HikariSub.TimeShift", "End times stay where they are, so a line shifted past its end ends before it starts.")
                              : qsTranslate("HikariSub.TimeShift", "Start times stay where they are, so a line shifted before its start ends before it starts."),
                severity: "warning",
                choices: [
                    { id: "shift", text: start ? qsTranslate("HikariSub.TimeShift", "Shift start times")
                                               : qsTranslate("HikariSub.TimeShift", "Shift end times"),
                      role: "accept", isDefault: true },
                    { id: "cancel", text: qsTranslate("HikariSub.TimeShift", "Cancel"), role: "reject" }
                ]
            }, id => {
                if (id === "shift" && root.app.editingTargetId() === target)
                    shiftTimesPanel.message = root.app.shiftTimes()
            })
            return
        }
        shiftTimesPanel.message = root.app.shiftTimes()
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
    function toggleMisspellPanel() {
        if (misspellDock.isOpen)
            misspellDock.close()
        else
            root.showPanel(misspellDock)
    }
    function showPanel(dock) {
        if (!root.panelAvailable(dock))
            return
        root.analytics.panelOpened(dock.uniqueName)
        if (dock === originalDock)
            root.openOriginal()
        else if (dock === logDock && !Docking.hasPlace("Log"))
            root.addLog()
        else if (dock === spellDock && !Docking.hasPlace("Spellchecker"))
            root.addSpellChecker()
        else if (dock === stylesDock && !Docking.hasPlace("Styles"))
            root.addStyles()
        else if (dock === misspellDock && !Docking.hasPlace("FixMinorErrors"))
            dockingArea.addDockWidget(misspellDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(420, 0))
        else if (dock === templateDock && !Docking.hasPlace("Template"))
            root.addTemplate()
        else
            dock.open()
        root.workspaceLayout.keepFloatingPanelsOnScreen()
        dock.raise()
        if (dock === spellDock && !spellTool.checking)
            spellTool.start()
        root.focusPanel(panels[dockList.indexOf(dock)], Qt.OtherFocusReason)
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
        UserGuide.open(i >= 0 ? "panel" : "", i >= 0 ? (panel.guideId || root.dockList[i].uniqueName) : "")
    }
    Connections {
        target: UserGuide
        function onOpenRequested(kind, id) { root.app.openUserGuide(kind, id) }
    }
    Shortcut {
        objectName: "dialogUserGuide"
        sequences: root.hotkeys.globalShortcuts.filter(s => s.symbol === "GLOBAL_HELP").map(s => s.keys)
        context: Qt.ApplicationShortcut
        enabled: !root.shellActive && root.workspaceLayout.focusWindow !== null
                 && root.workspaceLayout.focusWindow.guideId !== undefined
        onActivated: root.openUserGuide()
    }

    // Item flags are functions: a new model list makes the Instantiator remake every entry, the open one too.
    function panelOwnItems(dock) {
        if (dock === videoDock)
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
                      iconRole: "hotkeys", run: () => root.openSettings("hotkeys", 7) },
                    { name: "automationClearRuns", text: qsTranslate("HikariSub.Automation", "Clear runs"),
                      iconRole: "remove", enabled: () => root.automation.runs.count > (root.automation.running ? 1 : 0),
                      run: () => root.automation.runs.clear() }]
        if (dock === spellDock)
            return [{ name: "spellIgnoreComments", text: qsTranslate("HikariSub.Spelling", "Ignore comments"), checkable: true,
                      checked: () => spellTool.ignoreComments, run: () => spellTool.setIgnoreComments(!spellTool.ignoreComments) },
                    { name: "spellIgnoreUpper", text: qsTranslate("HikariSub.Spelling", "Ignore words in capitals"), checkable: true,
                      checked: () => spellTool.ignoreUpperCase,
                      run: () => spellTool.setIgnoreUpperCase(!spellTool.ignoreUpperCase) },
                    { name: "spellAddedWords", text: qsTranslate("HikariSub.Spelling", "Words added to dictionary…"),
                      iconRole: "dictionary", run: () => spellTool.openAddedWords() }]
        if (dock === gridDock)
            return [{ name: "foldGenerated", text: qsTranslate("HikariSub.Grid", "Fold generated lines"), checkable: true,
                      checked: () => root.shell.foldGenerated,
                      run: () => root.shell.foldGenerated = !root.shell.foldGenerated }]
        return []
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
        readonly property bool closable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotClosable) === 0
        readonly property bool dockable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotDockable) === 0
        property int tabIndex: -1
        property int tabCount: 0
        readonly property int leftStep: Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1
        function openFor(target, anchor) {
            dock = target
            tabIndex = Docking.tabIndex(target.uniqueName)
            tabCount = Docking.tabCount(target.uniqueName)
            margins = anchor.Window.window !== root ? Docking.windowShadow(anchor.Window.window) + 1 : 0
            popup(anchor, 0, anchor.height)
        }
        onAboutToShow: Docking.openMenu = dock ? dock.uniqueName : ""
        onAboutToHide: Docking.openMenu = ""
        Instantiator {
            model: panelOptionsMenu.ownItems
            delegate: ShellMenuItem {
                required property var modelData
                objectName: "panelOptions_" + modelData.name
                iconRole: modelData.iconRole ?? ""
                text: modelData.text
                checkable: modelData.checkable === true
                checked: typeof modelData.checked === "function" ? modelData.checked() : modelData.checked === true
                keepsMenuOpen: checkable && modelData.radio !== true
                enabled: typeof modelData.enabled === "function" ? modelData.enabled() : modelData.enabled !== false
                onTriggered: {
                    modelData.run()
                    // Clicking a checkable item assigns checked and breaks its binding; restore it.
                    if (typeof modelData.checked === "function")
                        checked = Qt.binding(() => modelData.checked())
                }
            }
            onObjectAdded: (index, object) => panelOptionsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeItem(object)
        }
        Instantiator {
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
            onObjectAdded: (index, object) => panelOptionsMenu.insertMenu(panelOptionsMenu.ownItems.length + index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeMenu(object)
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
            enabled: panelOptionsMenu.dockable
            onTriggered: placementWindow.openFor(panelOptionsMenu.dock)
        }
        ShellMenuItem {
            id: panelOptionsTabLeft
            iconRole: "tab-move-left"
            objectName: "panelOptionsTabLeft"
            text: qsTranslate("HikariSub.Workspace", "Move tab left")
            visible: panelOptionsMenu.tabCount > 1
            height: visible ? implicitHeight : 0
            enabled: panelOptionsMenu.tabIndex + panelOptionsMenu.leftStep >= 0
                     && panelOptionsMenu.tabIndex + panelOptionsMenu.leftStep < panelOptionsMenu.tabCount
            onTriggered: root.moveTab(panelOptionsMenu.dock, panelOptionsMenu.leftStep)
        }
        ShellMenuItem {
            iconRole: "tab-move-right"
            objectName: "panelOptionsTabRight"
            text: qsTranslate("HikariSub.Workspace", "Move tab right")
            visible: panelOptionsTabLeft.visible
            height: visible ? implicitHeight : 0
            enabled: panelOptionsMenu.tabIndex - panelOptionsMenu.leftStep >= 0
                     && panelOptionsMenu.tabIndex - panelOptionsMenu.leftStep < panelOptionsMenu.tabCount
            onTriggered: root.moveTab(panelOptionsMenu.dock, -panelOptionsMenu.leftStep)
        }
        ShellMenuItem {
            iconRole: panelOptionsMenu.dock && panelOptionsMenu.dock.isFloating ? "panel-dock" : "panel-float"
            objectName: "panelOptionsFloat"
            text: panelOptionsMenu.dock && panelOptionsMenu.dock.isFloating ? qsTranslate("HikariSub.Workspace", "Dock") : qsTranslate("HikariSub.Workspace", "Undock")
            enabled: panelOptionsMenu.dock !== null && (!panelOptionsMenu.dock.isFloating || panelOptionsMenu.dockable)
            onTriggered: root.setPanelFloating(panelOptionsMenu.dock, !panelOptionsMenu.dock.isFloating)
        }
        ShellMenuItem {
            iconRole: "tab-close"
            objectName: "panelOptionsClose"
            text: panelOptionsMenu.dock === referenceDock ? qsTranslate("HikariSub.Workspace", "Close reference") : qsTranslate("HikariSub.Workspace", "Close")
            enabled: panelOptionsMenu.closable
            onTriggered: root.closePanel(panelOptionsMenu.dock)
        }
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
        enabled: Qt.platform.os === "windows" && !root.hotkeys.globalSequences.includes("Shift+F10")
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
    SessionWindows {
        id: sessionWindows
        app: root.app
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
            Layout.leftMargin: titleBar.switcherGap
            onPresetChosen: name => root.applyPreset(name)
            onSavedLayoutChosen: name => root.applySavedLayout(name)
            onSaveRequested: saveLayoutDialog.show()
            onResetRequested: root.resetLayout()
            onManageRequested: manageLayoutsDialog.show()
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
                        text: qsTr("Open auto save")
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
                    iconRole: "import"
                    objectName: "importSettingsMenuItem"
                    text: qsTr("Import legacy settings...")
                    enabled: root.settingsImport.available
                    onTriggered: settingsImportDialog.openDialog()
                }
                ShellMenuItem {
                    help: qsTr("Exit the program")
                    iconRole: "exit"
                    objectName: "exitMenuItem"
                    action: Action {
                        text: qsTr("E&xit")
                        onTriggered: root.close()
                    }
                }
            }
        }
        ShellMenu {
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
                command: "GLOBAL_HISTORY"
                help: qsTr("History")
                iconRole: "history"
                objectName: "historyMenuItem"
                action: Action {
                    id: historyAction
                    text: qsTr("&History")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HISTORY")) historyWindow.open()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_MISSPELLS_REPLACER"
                help: qsTr("Shows or hides the Fix minor errors panel")
                iconRole: "multireplace"
                objectName: "misspellMenuItem"
                action: Action {
                    id: misspellAction
                    text: qsTr("Fix minor errors (experimental)")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_MISSPELLS_REPLACER")) root.toggleMisspellPanel()
                }
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
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SELECT_LINES")) selectLinesDialog.openDialog()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_REPLACE"
                help: qsTr("Searches for the specified text phrases and replaces them")
                iconRole: "find-replace"
                objectName: "findReplaceMenuItem"
                action: Action {
                    id: findReplaceAction
                    text: qsTr("Find and re&place")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_REPLACE")) root.openSearch(1)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SEARCH"
                help: qsTr("Searches for the specified text phrase")
                iconRole: "search"
                objectName: "findMenuItem"
                action: Action {
                    id: findAction
                    text: qsTr("&Find")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SEARCH")) root.openSearch(0)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_FIND_NEXT"
                help: qsTr("Finds the next occurrence of the phrase in the text")
                iconRole: "search"
                objectName: "findNextMenuItem"
                action: Action {
                    id: findNextAction
                    text: qsTr("Find next")
                    enabled: root.editor.hasLine && !root.app.findBusy
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_NEXT")) root.app.findNext()
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
                text: qsTr("Shift &times...")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_SHIFT_TIMES")) root.showPanel(timingDock)
            }
            ShellMenuItem {
                command: "GLOBAL_SHIFT_TIMES"
                id: runShiftTimesItem
                iconRole: "shift-times-run"
                objectName: "runShiftTimes"
                text: qsTr("Shift times / run timing postprocessor")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_TIMES")) root.runShiftTimes()
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_STYLE_MANAGER"
                help: qsTr("Is used to manage ASS styles")
                id: styleManagerItem
                iconRole: "styles"
                objectName: "styleManagerMenuItem"
                text: qsTr("Style &manager")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_STYLE_MANAGER")) styleManagerWindow.showFor(root.app.activeLineStyle())
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_ASS_PROPERTIES"
                help: qsTr("ASS subtitle properties")
                id: assPropertiesItem
                iconRole: "script-properties"
                objectName: "assProperties"
                text: qsTr("ASS file properties")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_ASS_PROPERTIES")) root.openScriptProperties()
            }
            ShellMenu {
                help: qsTr("Converts from one format to another")
                id: conversionMenu
                iconRole: "convert"
                objectName: "conversionMenu"
                title: qsTr("Conversion")
                property var targets: []
                onAboutToShow: targets = root.app.conversionTargets()
                Repeater {
                    model: root.conversionItems
                    ShellMenuItem {
                        objectName: "convertTo_" + modelData[0]
                        command: modelData[2]
                        help: modelData[3]
                        iconRole: ["convert-ass", "convert-srt", "convert-mdvd", "convert-mpl2", "convert-tmp"][index]
                        text: modelData[1]
                        enabled: conversionMenu.targets.indexOf(modelData[0]) >= 0
                        onTriggered: if (!root.hotkeyGesture(modelData[2])) root.openConversion(modelData[0], modelData[1])
                    }
                }
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
                iconRole: "karaoke"
                objectName: "checkKaraokeMenuItem"
                text: qsTr("Check karaoke")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_CHECK_KARAOKE")) root.openKaraokeCheck()
            }
            ShellMenuItem {
                command: "GLOBAL_KANJI_TIMER"
                help: qsTr("Copies the karaoke timing of romaji lines to their kanji lines")
                id: kanjiTimerItem
                iconRole: "karaoke-split"
                objectName: "kanjiTimerMenuItem"
                text: qsTr("Kanji timer…")
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
                help: qsTr("Plays / Pauses video")
                iconRole: root.video.playing ? "media-pause" : "media-play"
                action: Action {
                    id: playPauseAction
                    text: qsTr("Play / Pause"); enabled: root.video.hasVideo
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
            VideoStreamsMenu {
                objectName: "videoStreamsMenu"
                video: root.video
            }
            VideoChaptersMenu {
                objectName: "videoChaptersMenu"
                video: root.video
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
                    text: qsTr("Turn off video zoom"); enabled: root.videoView.zoomed
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_RESET_VIDEO_ZOOM")) root.videoView.resetZoom()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_VIDEO_RENDER_COST"
                iconRole: "render-cost"
                objectName: "renderCostMenuItem"
                action: Action {
                    id: renderCostAction
                    text: qsTr("Show / hide render cost"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_VIDEO_RENDER_COST")) root.videoView.toggleRenderCost()
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
                onPresetChosen: name => root.applyPreset(name)
                onSavedLayoutChosen: name => root.applySavedLayout(name)
                onSaveRequested: saveLayoutDialog.show()
                onResetRequested: root.resetLayout()
                onManageRequested: manageLayoutsDialog.show()
            }
            MenuSeparator {}
            ShellMenu {
                id: panelsMenu
                iconRole: "panels"
                objectName: "panelsMenu"
                title: qsTr("&Panels")
                Instantiator {
                    model: root.dockList
                    delegate: ShellMenu {
                        required property var modelData
                        objectName: "panelMenu" + modelData.uniqueName
                        title: modelData.title
                        ShellMenuItem {
                            iconRole: "show"
                            objectName: "panelShow" + modelData.uniqueName
                            text: qsTr("Show")
                            enabled: root.panelAvailable(modelData)
                            onTriggered: root.showPanel(modelData)
                        }
                        ShellMenuItem {
                            iconRole: "hide"
                            objectName: "panelHide" + modelData.uniqueName
                            text: qsTr("Hide")
                            enabled: modelData.isOpen
                            onTriggered: modelData.close()
                        }
                        ShellMenuItem {
                            iconRole: "panel-float"
                            objectName: "panelFloat" + modelData.uniqueName
                            text: qsTr("Float")
                            enabled: modelData.isOpen && !modelData.isFloating
                            onTriggered: root.setPanelFloating(modelData, true)
                        }
                        ShellMenuItem {
                            iconRole: "panel-dock"
                            objectName: "panelDock" + modelData.uniqueName
                            text: qsTr("Dock")
                            enabled: modelData.isOpen && modelData.isFloating
                            onTriggered: root.setPanelFloating(modelData, false)
                        }
                    }
                    onObjectAdded: (index, object) => panelsMenu.insertMenu(index, object)
                    onObjectRemoved: (index, object) => panelsMenu.removeMenu(object)
                }
            }
            ShellMenuItem {
                iconRole: "panel-move"
                objectName: "movePanel"
                text: qsTr("&Move panel…")
                onTriggered: placementWindow.openFor(root.focusedDock())
            }
            ShellMenuItem {
                iconRole: "layout-restore"
                objectName: "restoreLayoutBackup"
                text: qsTr("Restore the previous layout")
                enabled: root.workspaceLayout.hasBackup
                onTriggered: root.workspaceLayout.restoreBackup()
            }
            MenuSeparator {}
            ShellMenuItem {
                iconRole: "log"
                objectName: "logMenuItem"
                action: Action {
                    text: qsTr("Log")
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
                iconRole: "settings"
                objectName: "gettingStartedMenuItem"
                action: Action {
                    id: gettingStartedAction
                    text: qsTr("&Getting started")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GETTING_STARTED")) gettingStartedDialog.openSetup()
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
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ANSI")) reportIssueWindow.openForm()
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
                iconRole: "folder-open"
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
                    text: qsTr("What's &new")
                    onTriggered: whatsNewDialog.open()
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
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ABOUT")) aboutDialog.open()
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
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HELPERS")) creditsDialog.open()
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
        Component.onCompleted: sync()
        onTextChanged: if (!syncing) Qt.callLater(field.report)
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
        ContextMenu.onRequested: position => {
            field.misspellPosition = field.positionAt(position.x, position.y)
            field.grammar = field.spelled ? root.app.editorGrammarAt(field.role, field.misspellPosition) : ({})
            field.misspell = field.spelled && !field.grammar.rule
                ? root.app.editorMisspellAt(field.role, field.misspellPosition) : ({})
        }
        function ignoreGrammarRule() {
            const group = root.app.ignoreGrammarRule(field.grammar.rule)
            if (group < 0)
                return
            const name = field.grammar.groupName
            const groups = root.app.grammarGroups()
            const rules = group < groups.length ? groups[group].rules : 0
            root.notices.post("info", qsTranslate("HikariSub.Spelling", "%1 turned off").arg(name),
                              qsTranslate("HikariSub.Spelling", "Grammar check no longer uses its %n rules. Turn it back on in Settings › Spelling & grammar.", "", rules),
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
            MenuSeparator { visible: editMenu.suggestions.length > 0 || editMenu.grammar; height: visible ? implicitHeight : 0 }
            ShellMenuItem {
                iconRole: "hide"
                objectName: field.objectName + "IgnoreRule"
                text: qsTranslate("HikariSub.Spelling", "Ignore this rule")
                shortcutText: qsTranslate("HikariSub.Spelling", "Turns off %1").arg(field.grammar.groupName || "")
                visible: editMenu.grammar
                height: visible ? implicitHeight : 0
                onTriggered: field.ignoreGrammarRule()
            }
            ShellMenuItem {
                iconRole: "dictionary-add"
                objectName: field.objectName + "GrammarAddWord"
                text: qsTranslate("HikariSub.Spelling", "Add “%1” to dictionary").arg(field.grammar.text || "")
                visible: editMenu.grammar && !!field.grammar.word
                height: visible ? implicitHeight : 0
                onTriggered: {
                    if (!root.app.addGrammarWord(field.grammar.text))
                        root.notices.post("error", qsTranslate("HikariSub.Editor", "Add word to dictionary"),
                                          qsTranslate("HikariSub.Editor", "Error. Word \"%1\" was not added.").arg(field.grammar.text))
                }
            }
            MenuSeparator { visible: editMenu.grammar; height: visible ? implicitHeight : 0 }
            ShellMenuItem { iconRole: "edit-copy"; text: qsTranslate("HikariSub.Editor", "&Copy"); enabled: field.selectedText.length > 0; onTriggered: field.copy() }
            ShellMenuItem { iconRole: "edit-cut"; text: qsTranslate("HikariSub.Editor", "Cu&t"); enabled: field.selectedText.length > 0 && !field.readOnly; onTriggered: field.cut() }
            ShellMenuItem { iconRole: "edit-paste"; text: qsTranslate("HikariSub.Editor", "&Paste"); enabled: !field.readOnly; onTriggered: field.paste() }
            MenuSeparator {}
            ShellMenuItem {
                iconRole: "spellchecker"
                objectName: field.objectName + "SpellingOn"
                text: qsTranslate("HikariSub.Editor", "Spellchecker")
                checkable: true
                checked: root.app.spellingOn
                keepsMenuOpen: false
                visible: field.spelled
                height: visible ? implicitHeight : 0
                onTriggered: root.app.spellingOn = checked
            }
            ShellMenuItem {
                iconRole: "dictionary-add"
                objectName: field.objectName + "AddWord"
                text: qsTranslate("HikariSub.Editor", "&Add word \"%1\" to dictionary").arg(field.misspell.word || "")
                visible: !!field.misspell.word
                height: visible ? implicitHeight : 0
                onTriggered: {
                    if (!root.app.addEditorWord(field.misspell.word))
                        root.notices.post("error", qsTranslate("HikariSub.Editor", "Add word to dictionary"),
                                          qsTranslate("HikariSub.Editor", "Error. Word \"%1\" was not added.").arg(field.misspell.word))
                }
            }
            ShellMenuItem {
                iconRole: "delete"
                text: qsTranslate("HikariSub.Editor", "&Delete")
                enabled: field.selectedText.length > 0 && !field.readOnly
                onTriggered: field.remove(field.selectionStart, field.selectionEnd)
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
            if (!activeFocus)
                compositionEndedAt = -1
            else if (focusReason !== Qt.ActiveWindowFocusReason && focusReason !== Qt.PopupFocusReason)
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
        anchors.fill: dockingArea
        z: 1.5
        visible: root.app.home
        spaceAbove: documentTabBar.height
        onNewRequested: root.app.addPage()
        onOpenRequested: openAction.trigger()
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
        uniqueName: "Classic"
        enabled: !homePage.visible

        KDDW.DockWidget {
            id: videoDock
            objectName: "videoDock"
            uniqueName: "Video"
            title: qsTranslate("HikariSub.Video", "Video")
            Panel {
                id: videoPanel
                anchors.fill: parent
                objectName: "videoPanel"
                dockName: "Video"
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
                    } else if (event.key === Qt.Key_Menu) {
                        videoContextMenu.openAt(root.videoView.cursorIn(visualOverlay))
                        event.accepted = true
                    } else if (root.videoView.key(event.key, event.modifiers)) {
                        event.accepted = true
                    } else if (root.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                        event.accepted = true
                    }
                }
                Keys.onReleased: event => event.accepted = root.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

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
                VideoPresenter {
                    id: presenter
                    objectName: "videoPresenter"
                    visible: root.video.hasVideo
                    anchors.fill: parent
                    videoRect: root.visualTools.videoRect
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
                    visible: root.templateEditor.videoPreview && root.video.hasVideo
                    controller: root.templateEditor
                }
                VideoRenderCost {
                    anchors.fill: presenter
                    visible: root.videoView.renderCost && root.video.hasVideo
                    cost: presenter.renderCost
                    textTop: templateBadge.visible ? templateBadge.height + 12 : 8
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
                    onAspectRatioRequested: aspectRatioDialog.openAtCursor(visualOverlay)
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
                        Accessible.description: qsTranslate("HikariSub.Video", "Frame time; frame number; frames from the line's start frame; "
                                                     + "milliseconds from the line's start and end")
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
                        tip: root.bitmapTip(qsTranslate("HikariSub.Video", "Play / Pause"), "VIDEO_PLAY_PAUSE", 3)
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

        KDDW.DockWidget {
            id: audioDock
            objectName: "audioDock"
            uniqueName: "Audio"
            title: qsTranslate("HikariSub.Audio", "Audio")
            Panel {
                id: audioPanel
                anchors.fill: parent
                objectName: "audioPanel"
                dockName: "Audio"
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
                    if (alt) {
                        const second = root.boundKeys(alt, 4)
                        key = key + qsTranslate("HikariSub.Audio", " or ") + second
                    }
                    let tip = key.length ? text + " (" + key + ")" : text
                    tip += "\n" + qsTranslate("HikariSub.Audio", "Shortcut can be set using Shift + Click")
                    if (alt)
                        tip += "\n" + qsTranslate("HikariSub.Audio", "Second shortcut can be set using Control + Click")
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
                    visible: root.audio.loaded && side !== 0
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
                    component AudioRadioItem: ShellMenuItem {
                        id: radioItem
                        checkable: true
                        keepsMenuOpen: false
                        Accessible.role: Accessible.RadioButton
                        indicator: Rectangle {
                            readonly property color ink: radioItem.enabled && (radioItem.down || radioItem.highlighted)
                                                         ? radioItem.palette.highlightedText : radioItem.palette.text
                            x: radioItem.mirrored ? radioItem.width - width - radioItem.rightPadding : radioItem.leftPadding
                            y: radioItem.topPadding + (radioItem.availableHeight - height) / 2
                            implicitWidth: 14
                            implicitHeight: 14
                            radius: 7
                            color: "transparent"
                            border.color: ink
                            Rectangle {
                                anchors.centerIn: parent
                                width: 6
                                height: 6
                                radius: 3
                                color: parent.ink
                                visible: radioItem.checked
                            }
                        }
                    }
                    component AudioMenuHeading: Label {
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 4
                        bottomPadding: 2
                        color: Theme.muted
                        font.bold: true
                        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                    }
                    component PlayAroundItem: ShellMenuItem {
                        id: playItem
                        property string symbol
                        property string label
                        text: label + "\t" + root.boundKeys(symbol, 4)
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
                            menuTip: qsTranslate("HikariSub.Audio", "Commit options")
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
                                text: qsTranslate("HikariSub.Audio", "Commit") + "\t"
                                      + [root.boundKeys("AUDIO_COMMIT", 4), root.boundKeys("AUDIO_COMMIT_ALT", 4)]
                                        .filter(k => k.length).join(qsTranslate("HikariSub.Audio", " or "))
                                onTriggered: if (!root.hotkeyGesture("AUDIO_COMMIT", 4, true, true)) root.audio.commit()
                            }
                            MenuSeparator {}
                            ShellMenuItem {
                                id: audioAutoCommit
                                objectName: "audioAutoCommit"
                                iconRole: "auto-commit"
                                text: qsTranslate("HikariSub.Audio", "Automatically apply changes")
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
                                text: qsTranslate("HikariSub.Audio", "Go to the next line after applying changes")
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
                            text: qsTranslate("HikariSub.Audio", "Auto-commit")
                            tip: qsTranslate("HikariSub.Audio", "Automatically apply changes")
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
                            text: qsTranslate("HikariSub.Audio", "Next after commit")
                            tip: qsTranslate("HikariSub.Audio", "Go to the next line after applying changes")
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
                                tip: qsTranslate("HikariSub.Audio", "Enable / disable karaoke creation")
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
                                text: qsTranslate("HikariSub.Audio", "Split syllable at the cursor") + "\t"
                                      + root.boundKeys("AUDIO_KARAOKE_SPLIT", 4)
                                onTriggered: root.audio.splitSyllableAtCursor()
                            }
                            ShellMenuItem {
                                objectName: "audioKaraokeKf"
                                iconRole: "karaoke"
                                text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k", "\\kf") + "\t"
                                      + root.boundKeys("AUDIO_KARAOKE_KF", 4)
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
                        ShellMenu {
                            id: audioTapMenu
                            objectName: "audioTapMenu"
                            ShellMenuItem {
                                objectName: "audioTapStart"
                                iconRole: "karaoke-tap"
                                text: (root.audio.tapOn ? qsTranslate("HikariSub.Audio", "Stop tapping")
                                                        : qsTranslate("HikariSub.Audio", "Start tapping"))
                                      + "\t" + root.boundKeys("AUDIO_TAP", 4)
                                onTriggered: if (!root.hotkeyGesture("AUDIO_TAP", 4, true, false)) root.audio.toggleTap()
                            }
                            MenuSeparator {}
                            AudioMenuHeading {
                                text: qsTranslate("HikariSub.Audio", "Each syllable")
                            }
                            AudioRadioItem {
                                objectName: "audioTapHold"
                                text: qsTranslate("HikariSub.Audio", "Hold the key: releasing it leaves a gap")
                                checked: root.audio.tapHold
                                onTriggered: {
                                    root.audio.setTapHold(true)
                                    checked = Qt.binding(() => root.audio.tapHold)
                                }
                            }
                            AudioRadioItem {
                                objectName: "audioTapPress"
                                text: qsTranslate("HikariSub.Audio", "Press the key: no gaps")
                                checked: !root.audio.tapHold
                                onTriggered: {
                                    root.audio.setTapHold(false)
                                    checked = Qt.binding(() => !root.audio.tapHold)
                                }
                            }
                            MenuSeparator {}
                            ShellMenuItem {
                                objectName: "audioOnsetSnap"
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
                                id: audioTapSpeedMenu
                                objectName: "audioTapSpeedMenu"
                                iconRole: "playback-speed"
                                //: The Tap menu's Speed submenu; the current speed follows it
                                title: qsTranslate("HikariSub.Audio", "Speed") + "\t"
                                       + qsTranslate("HikariSub.Audio", "%1%").arg(root.audio.speedPercent)
                                Instantiator {
                                    model: [25, 33, 50, 75, 100]
                                    delegate: AudioRadioItem {
                                        required property int modelData
                                        objectName: "audioTapSpeed" + modelData
                                        text: qsTranslate("HikariSub.Audio", "%1%").arg(modelData)
                                        checked: root.audio.speedPercent === modelData
                                        onTriggered: {
                                            root.audio.setSpeedPercent(modelData)
                                            checked = Qt.binding(() => root.audio.speedPercent === modelData)
                                        }
                                    }
                                    onObjectAdded: (index, object) => audioTapSpeedMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => audioTapSpeedMenu.removeItem(object)
                                }
                            }
                            ShellMenuItem {
                                objectName: "audioTapCalibrate"
                                iconRole: "tap-calibrate"
                                //: The Tap menu's item; the tap latency follows it
                                text: qsTranslate("HikariSub.Audio", "Calibrate latency…") + "\t"
                                      + qsTranslate("HikariSub.Audio", "%1 ms").arg(root.audio.tapLatency)
                                onTriggered: tapCalibrationWindow.openWindow()
                            }
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
                            tip: qsTranslate("HikariSub.Audio", "Enhance speech frequencies in the spectrum")
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
                            visible: audioButtons.togglesFolded
                            iconRole: "panel-menu"
                            text: qsTranslate("HikariSub.Audio", "More audio options")
                            tip: text
                            down: audioMoreMenu.visible
                            onRun: audioMoreMenu.popup(audioMoreButton, 0, audioMoreButton.height)
                            ShellMenu {
                                id: audioMoreMenu
                                objectName: "audioMoreMenu"
                                component FoldedItem: ShellMenuItem {
                                    checkable: true
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedSeparator: MenuSeparator {
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedPlay: ShellMenuItem {
                                    property var play
                                    objectName: "audioMoreExtra" + play.id[0].toUpperCase() + play.id.slice(1)
                                    visible: audioButtons.foldsOut(play.id)
                                    height: visible ? implicitHeight : 0
                                    iconRole: play.icon
                                    text: play.label + "\t" + root.boundKeys(play.symbol, 4)
                                    onTriggered: if (!root.hotkeyGesture(play.symbol, 4, true, false)) root.audio.runHotkey(play.symbol)
                                }
                                FoldedPlay { play: audioPanel.play("beforeMark") }
                                FoldedPlay { play: audioPanel.play("afterMark") }
                                FoldedPlay { play: audioPanel.play("before") }
                                FoldedPlay { play: audioPanel.play("first") }
                                FoldedPlay { play: audioPanel.play("last") }
                                FoldedPlay { play: audioPanel.play("after") }
                                FoldedPlay { play: audioPanel.play("toEnd") }
                                ShellMenuItem {
                                    objectName: "audioMoreExtraAutoCommit"
                                    visible: audioButtons.foldsOut("autoCommit")
                                    height: visible ? implicitHeight : 0
                                    iconRole: "auto-commit"
                                    text: qsTranslate("HikariSub.Audio", "Automatically apply changes")
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
                                    text: qsTranslate("HikariSub.Audio", "Go to the next line after applying changes")
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
                                    text: qsTranslate("HikariSub.Audio", "Go to selection") + "\t" + root.boundKeys("AUDIO_GOTO", 4)
                                    onTriggered: if (!root.hotkeyGesture("AUDIO_GOTO", 4, true, false)) root.audio.goToSelection()
                                }
                                MenuSeparator {
                                    visible: audioButtons.kept.length < audioButtons.shownExtras.length
                                    height: visible ? implicitHeight : 0
                                }
                                FoldedItem {
                                    iconRole: "audio-line-mode"
                                    text: qsTranslate("HikariSub.Audio", "Line timing")
                                    checkable: true
                                    keepsMenuOpen: false
                                    checked: !root.audio.karaoke
                                    onTriggered: {
                                        checked = Qt.binding(() => !root.audio.karaoke)
                                        if (root.audio.karaoke) root.audio.toggleKaraoke()
                                    }
                                }
                                FoldedItem {
                                    objectName: "audioMoreKaraoke"
                                    iconRole: "karaoke"
                                    text: qsTranslate("HikariSub.Audio", "Karaoke")
                                    checkable: true
                                    keepsMenuOpen: false
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
                                    text: qsTranslate("HikariSub.Audio", "Split syllable at the cursor") + "\t"
                                          + root.boundKeys("AUDIO_KARAOKE_SPLIT", 4)
                                    onTriggered: root.audio.splitSyllableAtCursor()
                                }
                                FoldedItem {
                                    objectName: "audioMoreKaraokeKf"
                                    visible: audioButtons.splitArrowFolded && root.audio.karaoke
                                    checkable: false
                                    iconRole: "karaoke"
                                    text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k", "\\kf") + "\t"
                                          + root.boundKeys("AUDIO_KARAOKE_KF", 4)
                                    onTriggered: root.audio.switchLongSyllablesToKf()
                                }
                                FoldedItem {
                                    objectName: "audioMoreTap"
                                    visible: audioButtons.folded && root.audio.karaoke
                                    iconRole: "karaoke-tap"
                                    text: qsTranslate("HikariSub.Audio", "Tap to time") + "\t" + root.boundKeys("AUDIO_TAP", 4)
                                    checkable: true
                                    keepsMenuOpen: false
                                    checked: root.audio.tapOn
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.tapOn)
                                        root.audio.toggleTap()
                                    }
                                }
                                FoldedItem {
                                    objectName: "audioMoreTapOptions"
                                    visible: audioButtons.folded && root.audio.karaoke
                                    checkable: false
                                    keepsMenuOpen: false
                                    text: qsTranslate("HikariSub.Audio", "Tap options")
                                    onTriggered: Qt.callLater(() => audioTapMenu.popup(audioMoreButton, 0, audioMoreButton.height))
                                }
                                FoldedSeparator {}
                                FoldedItem {
                                    iconRole: "audio-waveform"
                                    text: qsTranslate("HikariSub.Audio", "Waveform")
                                    checkable: true
                                    keepsMenuOpen: false
                                    checked: !root.audio.spectrumOn
                                    onTriggered: {
                                        checked = Qt.binding(() => !root.audio.spectrumOn)
                                        root.audio.setSpectrumOn(false)
                                    }
                                }
                                FoldedItem {
                                    objectName: "audioMoreSpectrum"
                                    iconRole: "spectrum"
                                    text: qsTranslate("HikariSub.Audio", "Spectrum")
                                    checkable: true
                                    keepsMenuOpen: false
                                    checked: root.audio.spectrumOn
                                    onTriggered: {
                                        checked = Qt.binding(() => root.audio.spectrumOn)
                                        root.audio.setSpectrumOn(true)
                                    }
                                }
                                FoldedSeparator {}
                                ShellMenuItem {
                                    objectName: "audioMoreAutoScroll"
                                    iconRole: "auto-scroll"
                                    text: qsTranslate("HikariSub.Audio", "Auto-scroll")
                                    checkable: true
                                    checked: root.audio.autoScroll
                                    onToggled: {
                                        root.audio.setAutoScroll(checked)
                                        checked = Qt.binding(() => root.audio.autoScroll)
                                    }
                                }
                                ShellMenuItem {
                                    iconRole: "audio-video-sync"
                                    text: qsTranslate("HikariSub.Audio", "Sync with video")
                                    checkable: true
                                    checked: root.audio.syncVideo
                                    onToggled: {
                                        root.audio.setSyncVideo(checked)
                                        checked = Qt.binding(() => root.audio.syncVideo)
                                    }
                                }
                                ShellMenuItem {
                                    iconRole: "spectrum-nonlinear"
                                    text: qsTranslate("HikariSub.Audio", "Speech (non-linear spectrum)")
                                    checkable: true
                                    checked: root.audio.spectrumNonLinear
                                    onToggled: {
                                        root.audio.setSpectrumNonLinear(checked)
                                        checked = Qt.binding(() => root.audio.spectrumNonLinear)
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
                                    text: play.label + "\t" + root.boundKeys(play.symbol, 4)
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
                                    text: qsTranslate("HikariSub.Audio", "Auto-commit")
                                }
                                ShownItem {
                                    objectName: "audioCustomiseNextCommit"
                                    control: "nextCommit"
                                    iconRole: "next-after-commit"
                                    text: qsTranslate("HikariSub.Audio", "Go to the next line after commit")
                                }
                                ShownItem {
                                    objectName: "audioCustomiseGoto"
                                    control: "goto"
                                    iconRole: "go-to-selection"
                                    text: qsTranslate("HikariSub.Audio", "Go to selection") + "\t" + root.boundKeys("AUDIO_GOTO", 4)
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

        KDDW.DockWidget {
            id: originalDock
            objectName: "originalDock"
            uniqueName: "Original"
            title: qsTranslate("HikariSub.Translation", "Original text")
            Panel {
                id: originalPanel
                anchors.fill: parent
                objectName: "originalPanel"
                dockName: "Original"
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

        KDDW.DockWidget {
            id: editorDock
            objectName: "editorDock"
            uniqueName: "Editor"
            title: qsTranslate("HikariSub.Editor", "Line editor")
            Panel {
                id: editorPanel
                anchors.fill: parent
                objectName: "editorPanel"
                dockName: "Editor"
                minimumBodySize: Qt.size(Math.max(editorStrip.minimumWidth, editorInspector.minimumWidth)
                                         + 2 * (editorColumn.x - 4),
                                         editorStrip.implicitHeight
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
                            Layout.fillWidth: true
                            Layout.minimumWidth: minimumWidth
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
                            onStyleEditRequested: style => styleManagerWindow.editStyle(style)
                        }
                    }
                }
            }
        }

        KDDW.DockWidget {
            id: gridDock
            objectName: "gridDock"
            uniqueName: "Grid"
            title: qsTranslate("HikariSub.Grid", "Grid")
            Panel {
                id: gridPanel
                anchors.fill: parent
                objectName: "gridPanel"
                dockName: "Grid"
                minimumBodySize: Qt.size(240, grid.rowHeight * 3)
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Grid", "Editing: %1").arg(shell.editingTitle) : qsTranslate("HikariSub.Grid", "No document open")
                accessibleName: qsTranslate("HikariSub.Grid", "Grid")
                focus: true
                keyTarget: grid
                HikariGrid {
                    id: grid
                    objectName: "editingGrid"
                    anchors.fill: parent
                    focus: true
                    // Qt Quick puts only items with an Accessible attachment in the accessibility tree.
                    Accessible.role: Accessible.Table
                    model: shell.lines
                    onContentYChanged: root.app.setTargetScroll(Math.floor(contentY / rowHeight))
                    onActiveLineRequested: id => root.app.selectLine(id)
                    onActiveLineFallbackRequested: id => root.app.moveActiveLine(id)
                    onExtendRequested: rows => root.app.extendSelection(rows)
                    onLineClicked: (id, modifiers, endColumn, doubleClick) => root.app.clickLine(id, modifiers, endColumn, doubleClick)
                    onLineDragged: id => root.app.dragSelection(id)
                    onSelectAllRequested: root.app.selectAllLines()
                    onContextMenuRequested: (x, y) => gridMenu.popup(grid, x, y)
                    GridHeaderToolTip { grid: grid }
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
                        ShellMenuItem { iconRole: "tree-add-lines"; objectName: "groupAddLines"; text: qsTranslate("HikariSub.Grid", "Add lines"); onTriggered: root.app.addLinesToGroup(groupMenu.description) }
                        ShellMenuItem { iconRole: "edit-copy"; objectName: "groupCopy"; text: qsTranslate("HikariSub.Grid", "Copy line group"); onTriggered: root.app.copyGroup(groupMenu.description) }
                        ShellMenuItem {
                            iconRole: "edit"
                            objectName: "groupRename"; text: qsTranslate("HikariSub.Grid", "Change description")
                            onTriggered: groupDescriptionPopover.edit(groupMenu.description, grid, groupMenu.at)
                        }
                        ShellMenuItem { iconRole: "tree-select"; objectName: "groupSelect"; text: qsTranslate("HikariSub.Grid", "Select the group's lines"); onTriggered: root.app.selectGroup(groupMenu.description) }
                        ShellMenuItem { iconRole: "delete"; objectName: "groupDelete"; text: qsTranslate("HikariSub.Grid", "Delete"); onTriggered: root.app.removeGroup(groupMenu.description) }
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
                        onAboutToShow: {
                            canPasteTranslation = root.app.canPasteTranslation()
                            canShiftTranslation = root.app.canShiftTranslation()
                        }
                        ShellMenu {
                            iconRole: "add"
                            title: qsTranslate("HikariSub.Grid", "&Insert")
                            ShellMenuItem { command: "GRID_INSERT_BEFORE"; iconRole: "insert-before"; objectName: "insertBefore"; text: qsTranslate("HikariSub.Grid", "Insert &before"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE")) root.app.insertLine(true) }
                            ShellMenuItem { command: "GRID_INSERT_AFTER"; iconRole: "insert-after"; objectName: "insertAfter"; text: qsTranslate("HikariSub.Grid", "Insert &after"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER")) root.app.insertLine(false) }
                            ShellMenuItem {
                                command: "GRID_INSERT_BEFORE_VIDEO"
                                iconRole: "insert-before"
                                objectName: "insertBeforeVideo"; text: qsTranslate("HikariSub.Grid", "Insert before with &video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_VIDEO")) root.app.insertLine(true, "video")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_AFTER_VIDEO"
                                iconRole: "insert-after"
                                objectName: "insertAfterVideo"; text: qsTranslate("HikariSub.Grid", "Insert after with video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_VIDEO")) root.app.insertLine(false, "video")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME"
                                iconRole: "insert-before"
                                objectName: "insertBeforeFrame"; text: qsTranslate("HikariSub.Grid", "Insert before with video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME")) root.app.insertLine(true, "frame")
                            }
                            ShellMenuItem {
                                command: "GRID_INSERT_AFTER_WITH_VIDEO_FRAME"
                                iconRole: "insert-after"
                                objectName: "insertAfterFrame"; text: qsTranslate("HikariSub.Grid", "Insert after with video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_WITH_VIDEO_FRAME")) root.app.insertLine(false, "frame")
                            }
                        }
                        ShellMenuItem {
                            command: "GRID_DUPLICATE_LINES"
                            objectName: "duplicateLines"
                            iconRole: "duplicate"
                            readonly property string keys: root.boundKeys("GRID_DUPLICATE_LINES", 1)
                            text: qsTranslate("HikariSub.Grid", "&Duplicate lines") + (keys.length ? "\t" + keys : "")
                            onTriggered: if (!root.gridGesture(this, "GRID_DUPLICATE_LINES")) root.app.duplicateLines()
                        }
                        ShellMenuItem { command: "GRID_SWAP_LINES"; iconRole: "swap-lines"; objectName: "swapLines"; text: qsTranslate("HikariSub.Grid", "&Swap"); onTriggered: if (!root.gridGesture(this, "GRID_SWAP_LINES")) root.app.swapLines() }
                        ShellMenuItem { command: "GRID_JOIN_LINES"; iconRole: "join-lines"; objectName: "joinLines"; text: qsTranslate("HikariSub.Grid", "Join &lines"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_LINES")) root.app.joinLines("join") }
                        ShellMenuItem { command: "GRID_JOIN_TO_FIRST_LINE"; iconRole: "join-keep-first"; objectName: "joinFirst"; text: qsTranslate("HikariSub.Grid", "Join lines and keep first"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_FIRST_LINE")) root.app.joinLines("first") }
                        ShellMenuItem { command: "GRID_JOIN_TO_LAST_LINE"; iconRole: "join-keep-last"; objectName: "joinLast"; text: qsTranslate("HikariSub.Grid", "Join lines and keep last"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_LAST_LINE")) root.app.joinLines("last") }
                        ShellMenuItem {
                            command: "GRID_MAKE_CONTINOUS_PREVIOUS_LINE"
                            iconRole: "continuous-previous"
                            objectName: "continuousPrevious"; text: qsTranslate("HikariSub.Grid", "Make times continuous with the previous line")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_PREVIOUS_LINE")) root.app.makeContinuous(true)
                        }
                        ShellMenuItem {
                            command: "GRID_MAKE_CONTINOUS_NEXT_LINE"
                            iconRole: "continuous-next"
                            objectName: "continuousNext"; text: qsTranslate("HikariSub.Grid", "Make times continuous with the next line")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_NEXT_LINE")) root.app.makeContinuous(false)
                        }
                        ShellMenu {
                            iconRole: "split-lines"
                            title: qsTranslate("HikariSub.Grid", "Split lines")
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_VIDEO_TIME"
                                iconRole: "split-at-video"
                                objectName: "splitAtVideoTime"
                                text: qsTranslate("HikariSub.Grid", "Split line at video time")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_VIDEO_TIME")) root.app.splitLines("videoTime")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_FRAME"
                                iconRole: "split-frames"
                                objectName: "splitIntoFrames"
                                text: qsTranslate("HikariSub.Grid", "Split lines into frames")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_FRAME")) root.app.splitLines("frames")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_CHARS"
                                iconRole: "split-characters"
                                objectName: "splitIntoCharacters"
                                text: qsTranslate("HikariSub.Grid", "Split lines into characters")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_CHARS")) root.app.splitLines("chars")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_WORDS"
                                iconRole: "split-words"
                                objectName: "splitIntoWords"
                                text: qsTranslate("HikariSub.Grid", "Split lines into words")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WORDS")) root.app.splitLines("words")
                            }
                            ShellMenuItem {
                                command: "GRID_SPLIT_BY_WRAPS"
                                iconRole: "split-wraps"
                                objectName: "splitByWraps"
                                text: qsTranslate("HikariSub.Grid", "Split lines by wraps")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WRAPS")) root.app.splitLines("wraps")
                            }
                        }
                        ShellMenuItem {
                            command: "GRID_SELECT_VISIBLE_LINES"
                            iconRole: "select-visible"
                            objectName: "selectVisibleLines"; text: qsTranslate("HikariSub.Grid", "Select all lines visible on video")
                            onTriggered: if (!root.gridGesture(this, "GRID_SELECT_VISIBLE_LINES")) root.app.selectLinesVisibleOnVideo()
                        }
                        ShellMenuItem { command: "GRID_TREE_MAKE"; iconRole: "make-tree"; objectName: "makeTree"; text: qsTranslate("HikariSub.Grid", "Make line group"); onTriggered: if (!root.gridGesture(this, "GRID_TREE_MAKE")) root.app.makeGroups() }
                        ShellMenuItem {
                            command: "GRID_SHOW_PREVIEW"
                            iconRole: "reference"
                            objectName: "showPreview"
                            readonly property string keys: root.boundKeys("GRID_SHOW_PREVIEW", 1)
                            text: qsTranslate("HikariSub.Grid", "Show subtitles preview") + (keys.length ? "\t" + keys : "")
                            enabled: gridMenu.visible && root.app.canShowPreview()
                            onTriggered: if (!root.gridGesture(this, "GRID_SHOW_PREVIEW")) root.app.showPreview()
                        }
                        ShellMenuItem {
                            command: "GRID_PASTE_TRANSLATION"
                            iconRole: "paste-translation"
                            objectName: "pasteTranslation"
                            text: qsTranslate("HikariSub.Grid", "Paste translation text")
                            enabled: gridMenu.canPasteTranslation
                            help: root.app.translationHint
                            onTriggered: {
                                if (root.gridGesture(this, "GRID_PASTE_TRANSLATION"))
                                    return
                                translationFileDialog.currentFolder = root.app.targetFolder()
                                translationFileDialog.open()
                            }
                        }
                        ShellMenuItem {
                            command: "GRID_TRANSLATION_DIALOG"
                            iconRole: "translation-shift"
                            objectName: "translationDialog"
                            text: qsTranslate("HikariSub.Grid", "Dialogue shifting window")
                            enabled: gridMenu.canShiftTranslation
                            onTriggered: if (!root.gridGesture(this, "GRID_TRANSLATION_DIALOG")) translationShiftWindow.open()
                        }
                        ShellMenuItem { command: "GRID_HIDE_SELECTED"; iconRole: "hide"; objectName: "hideSelectedLines"; text: qsTranslate("HikariSub.Grid", "Hide selected lines"); onTriggered: if (!root.gridGesture(this, "GRID_HIDE_SELECTED")) root.app.hideSelectedLines() }
                        ShellMenu {
                            id: filteringMenu
                            objectName: "filteringMenu"
                            iconRole: "filter"
                            title: qsTranslate("HikariSub.Grid", "Filtering")
                            property var styleNames: []
                            onAboutToShow: styleNames = root.app.styleNames()
                            ShellMenuItem {
                                command: "GRID_FILTER_AFTER_SUBS_LOAD"
                                objectName: "filterAfterLoad"
                                text: qsTranslate("HikariSub.Grid", "Filter after loading subtitles")
                                checkable: true
                                enabled: root.shell.assColumns
                                checked: root.gridFilter.afterLoad
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_AFTER_SUBS_LOAD")) root.gridFilter.afterLoad = checked
                            }
                            ShellMenuItem {
                                command: "GRID_FILTER_INVERT"
                                objectName: "filterInvert"
                                text: qsTranslate("HikariSub.Grid", "Reverse filtering")
                                checkable: true
                                checked: root.gridFilter.inverted
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_INVERT")) root.gridFilter.inverted = checked
                            }
                            ShellMenuItem {
                                command: "GRID_FILTER_DO_NOT_RESET"
                                objectName: "filterDoNotReset"
                                text: qsTranslate("HikariSub.Grid", "Do not reset previous filtering")
                                checkable: true
                                checked: root.gridFilter.addToFilter
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_DO_NOT_RESET")) root.gridFilter.addToFilter = checked
                            }
                            ShellMenu {
                                id: filterStylesMenu
                                objectName: "filterStylesMenu"
                                iconRole: "styles"
                                title: qsTranslate("HikariSub.Grid", "Hide lines with styles")
                                enabled: root.shell.assColumns
                                Instantiator {
                                    model: filteringMenu.styleNames
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
                            Instantiator {
                                model: [
                                    { bit: 2, label: qsTranslate("HikariSub.Grid", "Hide selected lines"), name: "filterBySelection",
                                      symbol: "GRID_FILTER_BY_SELECTIONS" },
                                    { bit: 4, label: qsTranslate("HikariSub.Grid", "Hide comments"), name: "filterByComments", ass: true,
                                      symbol: "GRID_FILTER_BY_DIALOGUES" },
                                    { bit: 8, label: qsTranslate("HikariSub.Grid", "Show unconfirmed"), name: "filterByUnconfirmed", tl: true,
                                      symbol: "GRID_FILTER_BY_DOUBTFUL" },
                                    { bit: 16, label: qsTranslate("HikariSub.Grid", "Show untranslated"), name: "filterByUntranslated", tl: true,
                                      symbol: "GRID_FILTER_BY_UNTRANSLATED" }
                                ]
                                delegate: ShellMenuItem {
                                    required property var modelData
                                    objectName: modelData.name
                                    command: modelData.symbol
                                    text: modelData.label
                                    checkable: true
                                    enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.tl || root.editor.translationMode)
                                    checked: (root.gridFilter.filterBy & modelData.bit) !== 0
                                    onTriggered: if (!root.gridGesture(this, modelData.symbol)) root.gridFilter.setFilterBy(modelData.bit, checked)
                                }
                                onObjectAdded: (index, object) => filteringMenu.insertItem(4 + index, object)
                                onObjectRemoved: (index, object) => filteringMenu.removeItem(object)
                            }
                            ShellMenuItem { command: "GRID_FILTER"; iconRole: "filter"; objectName: "filter"; text: qsTranslate("HikariSub.Grid", "Filter"); onTriggered: if (!root.gridGesture(this, "GRID_FILTER")) root.app.filterLines() }
                            ShellMenuItem {
                                command: "GRID_FILTER_BY_NOTHING"
                                iconRole: "filter-off"
                                objectName: "turnOffFiltering"
                                text: qsTranslate("HikariSub.Grid", "Turn off filtering")
                                enabled: root.shell.filtered
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_BY_NOTHING")) root.app.turnOffFiltering()
                            }
                        }
                        ShellMenuItem {
                            command: "GRID_FILTER_IGNORE_IN_ACTIONS"
                            objectName: "ignoreFilteringInActions"
                            text: qsTranslate("HikariSub.Grid", "Ignore filtering in some actions")
                            checkable: true
                            checked: root.gridFilter.ignoreInActions
                            onTriggered: if (!root.gridGesture(this, "GRID_FILTER_IGNORE_IN_ACTIONS")) root.gridFilter.ignoreInActions = checked
                        }
                        ShellMenu {
                            id: hideColumnsMenu
                            iconRole: "columns"
                            objectName: "hideColumnsMenu"
                            title: qsTranslate("HikariSub.Grid", "Hide columns")
                            ShellMenuItem {
                                objectName: "hideEmptyColumns"
                                text: qsTranslate("HikariSub.Grid", "Hide empty columns")
                                checkable: true
                                checked: root.shell.autoHideColumns
                                onTriggered: root.shell.autoHideColumns = checked
                            }
                            MenuSeparator {}
                            Instantiator {
                                model: [
                                    { bit: 1, label: qsTranslate("HikariSub.Grid", "Hide layer"), ass: true, symbol: "GRID_HIDE_LAYER" },
                                    { bit: 2, label: qsTranslate("HikariSub.Grid", "Hide start time"), ass: false, symbol: "GRID_HIDE_START" },
                                    { bit: 4, label: qsTranslate("HikariSub.Grid", "Hide end time"), ass: false, end: true, symbol: "GRID_HIDE_END" },
                                    { bit: 16, label: qsTranslate("HikariSub.Grid", "Hide actor"), ass: true, symbol: "GRID_HIDE_ACTOR" },
                                    { bit: 8, label: qsTranslate("HikariSub.Grid", "Hide style"), ass: true, symbol: "GRID_HIDE_STYLE" },
                                    { bit: 32, label: qsTranslate("HikariSub.Grid", "Hide left margin"), ass: true, symbol: "GRID_HIDE_MARGINL" },
                                    { bit: 64, label: qsTranslate("HikariSub.Grid", "Hide right margin"), ass: true, symbol: "GRID_HIDE_MARGINR" },
                                    { bit: 128, label: qsTranslate("HikariSub.Grid", "Hide vertical margin"), ass: true, symbol: "GRID_HIDE_MARGINV" },
                                    { bit: 256, label: qsTranslate("HikariSub.Grid", "Hide effect"), ass: true, symbol: "GRID_HIDE_EFFECT" },
                                    { bit: 512, label: qsTranslate("HikariSub.Grid", "Hide characters per second"), ass: false, symbol: "GRID_HIDE_CPS" },
                                    { bit: 8192, label: qsTranslate("HikariSub.Grid", "Hide line wraps"), ass: false, symbol: "GRID_HIDE_WRAPS" }
                                ]
                                delegate: ShellMenuItem {
                                    required property var modelData
                                    objectName: "hideColumn" + modelData.bit
                                    command: modelData.symbol
                                    text: modelData.label
                                    checkable: true
                                    checked: (root.shell.hiddenColumns & modelData.bit) !== 0
                                    enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.end || root.shell.endColumn)
                                    onTriggered: if (!root.gridGesture(this, modelData.symbol)) root.shell.toggleColumn(modelData.bit)
                                }
                                onObjectAdded: (index, object) => hideColumnsMenu.insertItem(2 + index, object)
                                onObjectRemoved: (index, object) => hideColumnsMenu.removeItem(object)
                            }
                        }
                        ShellMenuItem { command: "GRID_SET_NEW_FPS"; iconRole: "fps"; objectName: "setNewFps"; text: qsTranslate("HikariSub.Grid", "Set new FPS"); onTriggered: if (!root.gridGesture(this, "GRID_SET_NEW_FPS")) root.openSetNewFps() }
                        ShellMenuItem {
                            command: "GRID_SET_FPS_FROM_VIDEO"
                            iconRole: "fps-from-video"
                            objectName: "setFpsFromVideo"; text: qsTranslate("HikariSub.Grid", "Set FPS from video")
                            enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_SET_FPS_FROM_VIDEO")) root.app.setFpsFromVideo()
                        }
                        ShellMenuItem { command: "GRID_COPY"; iconRole: "edit-copy"; objectName: "copyLines"; text: qsTranslate("HikariSub.Grid", "Copy\tCtrl+C"); onTriggered: if (!root.gridGesture(this, "GRID_COPY")) root.app.copyLines() }
                        ShellMenuItem { command: "GRID_CUT"; iconRole: "edit-cut"; objectName: "cutLines"; text: qsTranslate("HikariSub.Grid", "Cut\tCtrl+X"); onTriggered: if (!root.gridGesture(this, "GRID_CUT")) root.app.cutLines() }
                        ShellMenuItem { command: "GRID_PASTE"; iconRole: "edit-paste"; objectName: "pasteLines"; text: qsTranslate("HikariSub.Grid", "Paste\tCtrl+V"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE")) root.app.pasteLines() }
                        ShellMenuItem { command: "GRID_COPY_COLUMNS"; iconRole: "columns-copy"; objectName: "copyColumns"; text: qsTranslate("HikariSub.Grid", "Copy columns"); onTriggered: if (!root.gridGesture(this, "GRID_COPY_COLUMNS")) columnsWindow.choose(false) }
                        ShellMenuItem { command: "GRID_PASTE_COLUMNS"; iconRole: "columns-paste"; objectName: "pasteColumns"; text: qsTranslate("HikariSub.Grid", "Paste columns"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE_COLUMNS")) columnsWindow.choose(true) }
                        ShellMenuItem { command: "GLOBAL_REMOVE_TEXT"; iconRole: "clear"; objectName: "deleteText"; text: qsTranslate("HikariSub.Grid", "Delete text"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_TEXT")) root.app.deleteText() }
                        ShellMenuItem { command: "GLOBAL_REMOVE_LINES"; iconRole: "delete"; objectName: "deleteLines"; text: qsTranslate("HikariSub.Grid", "Delete lines\tShift+Del"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_LINES")) root.app.deleteLines() }
                        MenuSeparator {}
                        ShellMenuItem {
                            command: "GLOBAL_OPEN_FONT_COLLECTOR"
                            iconRole: "font-collector"
                            objectName: "gridFontCollector"; text: qsTranslate("HikariSub.Grid", "Font collector"); enabled: root.shell.assColumns
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_OPEN_FONT_COLLECTOR")) root.openFontCollector()
                        }
                        ShellMenuItem {
                            command: "GRID_SUBS_FROM_MKV"
                            iconRole: "extract-subtitles"
                            objectName: "gridSubsFromMkv"; text: qsTranslate("HikariSub.Grid", "Load subtitles from an MKV/OGM file")
                            enabled: root.matroska.available
                            onTriggered: if (!root.gridGesture(this, "GRID_SUBS_FROM_MKV")) matroskaSubtitles.begin()
                        }
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

        KDDW.DockWidget {
            id: referenceDock
            objectName: "referenceDock"
            uniqueName: "Reference"
            title: shell.hasReference ? qsTranslate("HikariSub.Reference", "Reference: %1").arg(shell.referenceTitle) : qsTranslate("HikariSub.Reference", "Reference")
            Panel {
                id: referencePanel
                anchors.fill: parent
                objectName: "referencePanel"
                dockName: "Reference"
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

        KDDW.DockWidget {
            id: timingDock
            objectName: "timingDock"
            uniqueName: "Timing"
            title: qsTranslate("HikariSub.TimeShift", "Shift times")
            Panel {
                id: timingPanel
                objectName: "timingPanel"
                dockName: "Timing"
                minimumBodySize: shiftTimesPanel.minimumBodySize
                anchors.fill: parent
                title: qsTranslate("HikariSub.TimeShift", "Shift times")
                ShiftTimesPanel {
                    id: shiftTimesPanel
                    anchors.fill: parent
                    shiftTimes: root.shiftTimes
                    app: root.app
                    hasVideo: root.video.hasVideo
                    audioHasMark: root.audio.hasMark
                    onRun: root.runShiftTimes()
                }
            }
        }

        KDDW.DockWidget {
            id: searchDock
            objectName: "searchDock"
            uniqueName: "Search"
            title: qsTranslate("HikariSub.Search", "Search")
            onIsOpenChanged: if (!isOpen) searchTool.save()
            Panel {
                id: searchPanel
                objectName: "searchPanel"
                dockName: "Search"
                minimumBodySize: searchTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.Search", "Find and replace")
                onActiveFocusChanged: if (activeFocus) searchTool.activated()
                SearchTool {
                    id: searchTool
                    anchors.fill: parent
                    app: root.app
                }
            }
        }

        KDDW.DockWidget {
            id: automationDock
            objectName: "automationDock"
            uniqueName: "Automation"
            title: qsTranslate("HikariSub.Automation", "Automation")
            Panel {
                id: automationPanel
                objectName: "automationPanel"
                dockName: "Automation"
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

        KDDW.DockWidget {
            id: spellDock
            objectName: "spellDock"
            uniqueName: "Spellchecker"
            title: qsTranslate("HikariSub.Spelling", "Spellchecker")
            onIsOpenChanged: if (!isOpen) spellTool.stop()
            Panel {
                id: spellPanel
                objectName: "spellPanel"
                dockName: "Spellchecker"
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
                    notices: root.notices
                    documentName: root.shell.editingTitle
                    onSettingsRequested: root.openSettings("spelling")
                }
            }
        }

        KDDW.DockWidget {
            id: stylesDock
            objectName: "stylesDock"
            uniqueName: "Styles"
            title: qsTranslate("HikariSub.Styles", "Styles")
            Panel {
                id: stylesPanel
                objectName: "stylesPanel"
                dockName: "Styles"
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
                    editor: root.editor
                    app: root.app
                    manager: styleManagerWindow
                    catalogs: root.fontCatalogs
                    live: stylesDock.isOpen
                }
            }
        }

        KDDW.DockWidget {
            id: templateDock
            objectName: "templateDock"
            uniqueName: "Template"
            title: qsTranslate("HikariSub.TemplateEditor", "Template editor")
            Binding {
                target: root.templateEditor
                property: "shown"
                value: templateDock.isOpen && templatePanel.visible
            }
            Panel {
                id: templatePanel
                objectName: "templatePanel"
                dockName: "Template"
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

        KDDW.DockWidget {
            id: logDock
            objectName: "logDock"
            uniqueName: "Log"
            title: qsTr("Log")
            Panel {
                id: logPanel
                objectName: "logPanel"
                dockName: "Log"
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
        KDDW.DockWidget {
            id: misspellDock
            objectName: "misspellDock"
            uniqueName: "FixMinorErrors"
            title: qsTranslate("HikariSub.MisspellReplacer", "Fix minor errors")
            // MUST KILL: deferred because the default arrangement opens the panel and closes it at once
            onIsOpenChanged: if (isOpen) Qt.callLater(misspellDock.reloadIfOpen)
            function reloadIfOpen() {
                if (misspellDock.isOpen)
                    misspellTool.reloadRules()
            }
            Panel {
                id: misspellPanel
                objectName: "misspellPanel"
                dockName: "FixMinorErrors"
                minimumBodySize: misspellTool.minimumSize
                anchors.fill: parent
                title: qsTranslate("HikariSub.MisspellReplacer", "Fix minor errors")
                FixMinorErrors {
                    id: misspellTool
                    anchors.fill: parent
                    app: root.app
                    Connections {
                        target: root.app
                        function onMisspellReplacerDestroyed() {
                            misspellTool.reset()
                            misspellDock.reloadIfOpen()
                        }
                    }
                }
            }
        }

        Component.onCompleted: {
            Docking.setPanelHeader("Grid", true, null)
            Docking.setPanelHeader("Reference", true, referenceTray.toolbar)
            Docking.setPanelHeader("Search", true, null)
            Docking.setPanelHeader("Log", true, null)
            Docking.setPanelHeader("Spellchecker", true, null)
            Docking.setPanelHeader("Original", true, originalTools)
            Docking.setPanelHeader("Styles", true, stylesTool.toolbar)
            Docking.setPanelHeader("Template", false, templateTool.toolbar)
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
            Layout.fillWidth: true
            onCloseRequested: index => root.closeTab(index)
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
                Button {
                    text: qsTranslate("HikariSub.Workspace", "Restore the previous layout")
                    visible: root.workspaceLayout.hasBackup
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
                root.close()
            }
        }
        InlineBar {
            id: startupBar
            objectName: "startupBar"
            readonly property StartupOffer offer: sessionWindows.offer
            visible: offer.active && !root.app.home
            severity: offer.kind === "crash" ? "warning" : "info"
            text: offer.barText
            Layout.fillWidth: true
            onClosed: offer.end()
            Button {
                objectName: "startupRestore"
                visible: startupBar.offer.restorable
                text: startupBar.offer.kind === "crash" ? qsTranslate("HikariSub.Session", "Restore session")
                                                        : qsTranslate("HikariSub.Session", "Restore")
                onClicked: startupBar.offer.restore()
            }
            Button {
                objectName: "startupAutosaves"
                visible: startupBar.offer.kind === "crash" && startupBar.offer.autosaveCount > 0
                text: qsTranslate("HikariSub.Session", "Autosaves…")
                onClicked: autosaves.show()
            }
        }
        InlineBar {
            objectName: "mkvProgress"
            visible: root.matroska.state === 3
                     && (root.mkvTab === "home" ? root.app.home : !root.app.home && root.currentTab().id === root.mkvTab)
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
            Button {
                objectName: "reloadYes"
                text: qsTranslate("HikariSub.Documents", "Reload")
                onClicked: {
                    const id = diskChangeBar.tab.id
                    if (root.app.reloadTarget())
                        root.endDiskChange(id)
                }
            }
            Button {
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
                    Button {
                        objectName: "restoreRetry" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Retry")
                        onClicked: sessionWindows.retry(entryBar.modelData)
                    }
                    Button {
                        objectName: "restoreRelink" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Relink…")
                        onClicked: sessionWindows.relink(entryBar.modelData)
                    }
                    Button {
                        objectName: "restoreRemove" + entryBar.index
                        text: qsTranslate("HikariSub.Session", "Remove")
                        onClicked: sessionWindows.remove(entryBar.modelData)
                    }
                }
            }
        }
    }

    footer: StatusBar {
        id: statusStrip
        shell: root.shell
        fieldsController: root.statusBar
        selectionText: root.shell.selectionStatus
        saveText: root.editor.saveStatus
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
    SaveLayoutDialog {
        id: saveLayoutDialog
        layouts: root.workspaceLayout
        settings: root.app.settings
    }
    ManageLayoutsDialog {
        id: manageLayoutsDialog
        layouts: root.workspaceLayout
        settings: root.app.settings
    }

    TapCalibrationWindow {
        id: tapCalibrationWindow
        audio: root.audio
    }

    HistoryWindow {
        id: historyWindow
        editor: root.editor
        documentName: root.shell.editingTitle
        target: {
            const tab = root.app.tabs[root.app.currentTab]
            return tab ? tab.id : null
        }
        settings: root.app.settings
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
        else if (document === 0)
            return root.editor.save()
        else
            return root.app.saveDocument(document)
        return true
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
        else
            openSaveDialog(next.id)
    }
    function openSaveDialog(id) {
        const v = root.app.saveDialogValuesFor(id ?? 0)
        saveAsDialog.document = id ?? 0
        saveAsDialog.title = qsTranslate("HikariSub.Documents", "Save “%1” as").arg(root.tabTitle(id ?? 0))
        saveAsDialog.nameFilters = [v.filter, qsTranslate("HikariSub.Documents", "All files (*)")]
        if (v.folder.toString() !== "")
            saveAsDialog.currentFolder = v.folder
        else if (root.pickerFolder("subtitles").length > 0)
            saveAsDialog.currentFolder = root.pickerFolder("subtitles")
        saveAsDialog.selectedFile = v.file
        saveAsDialog.open()
    }
    FileDialog {
        id: saveAsDialog
        objectName: "saveAsDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTranslate("HikariSub.Documents", "Subtitle file ") + "(*.ass)"]
        // MUST KILL: document id 0 means the editing target
        property var document: 0
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            const result = root.app.saveChosenFor(document, selectedFile)
            if (result === "readonly") {
                root.warnReadOnly(document, root.app.localPath(selectedFile).replace(/^.*[\\/]/, ""))
            } else {
                const id = document === 0 ? root.currentTab().id : document
                if (result === "" && root.changedOnDisk[id] === true)
                    root.savingAway = Object.assign({}, root.savingAway, { [id]: true })
                matroskaSubtitles.saveDone()
                root.saveNext()
            }
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
        confirmOverDisk: (ids, then, cancelled) => {
            const marked = ids.filter(id => root.changedOnDisk[id] === true)
            if (marked.length === 0)
                then()
            else
                diskSaveQuestion.confirm(marked, then, cancelled)
        }
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
    ScriptResolutionDialog {
        prompt: root.automationResolution
    }
    AutomationNotices {
        automation: root.automation
        toasts: root.notices
        messages: root.messages
    }
    FileDialog {
        id: scriptDialog
        nameFilters: [qsTranslate("HikariSub.Automation", "Automation scripts (*.lua *.moon)"), qsTranslate("HikariSub.Automation", "All files (*)")]
        onAccepted: root.automation.loadScript(selectedFile)
    }

    FileDialog {
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

    AspectRatioDialog {
        id: aspectRatioDialog
        view: root.videoView
        video: root.video
        settings: root.app.settings
    }

    VideoFullscreen {
        id: videoFullscreenWindow
        shell: root
        controller: root.videoFullscreen
        onOpenVideoRequested: videoDialog.show()
        onOpenSubtitlesRequested: openDialog.show()
        onAspectRatioRequested: aspectRatioDialog.openAtCursor(videoFullscreenWindow.contentItem)
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

    FileDialog {
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
                      qsTranslate("HikariSub.Documents", "Word-timed lyrics (*.lrc *.ttml)"),
                      qsTranslate("HikariSub.Documents", "Videos with embedded subtitles (*.mkv *.ogm)"),
                      qsTranslate("HikariSub.Documents", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            root.openChosenSubtitles(root.app.localPath(selectedFile))
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

    ColumnsWindow {
        id: columnsWindow
        app: root.app
        settings: root.app.settings
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

    FileDialog {
        id: audioDialog
        objectName: "audioDialog"
        title: qsTranslate("HikariSub.Audio", "Choose audio file")
        nameFilters: [qsTranslate("HikariSub.Audio", "Audio and video files") + " (*.wav *.w64 *.flac *.ac3 *.aac *.ogg *.mp3 *.mp4 *.m4a *.mkv *.avi)",
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
    FileDialog {
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
    FileDialog {
        id: translationFileDialog
        title: qsTranslate("HikariSub.Translation", "Choose subtitle file")
        nameFilters: [qsTranslate("HikariSub.Translation", "Subtitle files (*.ass *.srt *.sub *.txt)")]
        onAccepted: root.app.pasteTranslationFile(selectedFile)
    }
    TranslationShiftWindow {
        id: translationShiftWindow
        app: root.app
        settings: root.app.settings
    }
    HidableQuestion {
        id: shiftQuestion
        objectName: "shiftQuestion"
        messages: root.messages
        questionId: "shiftOneTime"
        property var askedFor: 0
        property Connections targetChanges: Connections {
            target: root.app
            function onTabsChanged() {
                if (shiftQuestion.pending.length > 0 && root.app.editingTargetId() !== shiftQuestion.askedFor)
                    shiftQuestion.withdraw()
            }
        }
    }
    Connections {
        target: root.updates
        function onFinished(outcome, release, interactive) {
            if (outcome === "available") {
                updateAvailable.showRelease(release, !interactive)
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
        function onReady() { updateAvailable.close() }
    }
    UpdateAvailable {
        id: updateAvailable
        updates: root.updates
        app: root.app
        whatsNew: root.whatsNew
        settings: root.app.settings
    }
    WhatsNewDialog {
        id: whatsNewDialog
        whatsNew: root.whatsNew
        settings: root.app.settings
    }
    AboutDialog {
        id: aboutDialog
        app: root.app
        updates: root.updates
        settings: root.app.settings
        onReportIssueRequested: reportIssueWindow.openForm()
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
    ReportIssueWindow {
        id: reportIssueWindow
        reports: root.crashReports
        app: root.app
        mainWindow: root
        settings: root.app.settings
        onBrowserFailed: root.issueBrowserFailed()
    }
    function issueBrowserFailed() {
        root.notices.post("warning", qsTranslate("HikariSub.Reports", "The browser did not open"),
                          qsTranslate("HikariSub.Reports", "The issue is on the clipboard. Paste it into a new issue on GitHub."))
    }
    Connections {
        target: root.crashReports
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
            root.notices.post("warning", qsTranslate("HikariSub.Reports", "The crash report could not be sent"),
                              source === "start" ? qsTranslate("HikariSub.Reports", "It is kept for the next start.") : "",
                              { actions: source === "" ? [] : [{ text: qsTranslate("HikariSub.Reports", "Try again"), run: () => root.crashReports.send(source, note) },
                                                               { text: qsTranslate("HikariSub.Reports", "Open GitHub issue"), run: () => crashReportWindow.openOnGitHub(source, note) }] })
        }
        function onAutoSent(kind, ok, source) {
            if (!ok) {
                root.notices.post("warning", qsTranslate("HikariSub.Reports", "The crash report could not be sent"),
                                  source === "start" ? qsTranslate("HikariSub.Reports", "It is kept for the next start.") : "",
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
    CreditsDialog {
        id: creditsDialog
        app: root.app
        settings: root.app.settings
    }
    function openConversion(target, label) {
        conversionLoader.active = true
        conversionLoader.item.openFor(target, label)
    }
    Loader {
        id: conversionLoader
        active: false
        sourceComponent: ConversionWindow {
            app: root.app
            catalogs: root.styleManager.catalogs
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
    Connections {
        target: root.app
        function onResolutionMismatch(sizes) { root.askResolutionMismatch(sizes) }
    }
    StyleManager {
        id: styleManagerWindow
        styles: root.styleManager
        catalogs: root.fontCatalogs
        colourPicker: root.colourPicker
        app: root.app
        notices: root.notices
    }
    SettingsImportDialog {
        id: settingsImportDialog
        importer: root.settingsImport
        settings: root.app.settings
    }
    GettingStartedDialog {
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
    Loader {
        id: settingsLoader
        objectName: "settingsLoader"
        active: false
        sourceComponent: Component {
            SettingsDialog {
                app: root.app
                notices: root.notices
                hotkeys: root.hotkeys
                updates: root.updates
                crashReports: root.crashReports
                colourPicker: root.colourPicker
                tagButtons: root.tagButtons
                onCreditsRequested: creditsDialog.open()
            }
        }
    }
    function settingsWindow() {
        settingsLoader.active = true
        return settingsLoader.item
    }
    function openSettings(page, filter) { settingsWindow().openDialog(page, filter) }
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
            styleManagerWindow.showFor(style)
        }
    }
    SelectLinesDialog {
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
        function onGroupBreakRefused(description, title) {
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

    Component.onCompleted: {
        root.applyMenuShortcuts()
        const turnedOn = root.app.settings.value("workspace.karaokeTurnedOn")
        root.karaokeTurnedOn = turnedOn.indexOf("audio.karaoke") >= 0
        root.foldTurnedOn = turnedOn.indexOf("grid.foldGenerated") >= 0
        if (root.karaokeTurnedOn || root.foldTurnedOn)
            root.workspaceLayout.resumeMode("Karaoke")
        if (root.app.gettingStartedDue())
            Qt.callLater(() => gettingStartedDialog.openSetup())
        if (root.crashReports.pending.length > 0
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
    function mappedTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shortcut can be set using Shift + Click")
    }
    function bitmapTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shortcut can be set using Shift + Click")
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
        })
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
    function globalActions() {
        return {
        GLOBAL_SAVE_SUBS: saveAction, GLOBAL_SAVE_ALL_SUBS: saveAllAction, GLOBAL_SAVE_SUBS_AS: saveAsAction,
        GLOBAL_SAVE_TRANSLATION: saveTranslationAction, GLOBAL_REMOVE_SUBS: removeSubsAction,
        GLOBAL_REDO: redoAction, GLOBAL_UNDO: undoAction, GLOBAL_UNDO_TO_LAST_SAVE: undoToLastSaveAction,
        GLOBAL_HISTORY: historyAction, GLOBAL_SEARCH: findAction, GLOBAL_FIND_REPLACE: findReplaceAction,
        GLOBAL_FIND_NEXT: findNextAction, GLOBAL_MISSPELLS_REPLACER: misspellAction,
        GLOBAL_OPEN_SELECT_LINES: selectLinesAction, GLOBAL_OPEN_AUDIO: openAudioAction,
        GLOBAL_AUDIO_FROM_VIDEO: audioFromVideoAction, GLOBAL_CLOSE_AUDIO: closeAudioAction,
        GLOBAL_AUTOMATION_LOAD_SCRIPT: loadScriptAction, GLOBAL_AUTOMATION_RELOAD_AUTOLOAD: reloadAutoloadAction,
        GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT: loadLastScriptAction,
        GLOBAL_AUTOMATION_REPEAT_LAST_RUN: repeatRunAction,
        GLOBAL_PLAY_PAUSE: playPauseAction,
        GLOBAL_PREVIOUS_FRAME: previousFrameAction, GLOBAL_NEXT_FRAME: nextFrameAction,
        GLOBAL_SET_VIDEO_AT_START_TIME: goToStartAction, GLOBAL_SET_VIDEO_AT_END_TIME: goToEndAction,
        GLOBAL_SET_START_TIME: setStartTimeAction, GLOBAL_SET_END_TIME: setEndTimeAction,
        GLOBAL_GO_TO_NEXT_KEYFRAME: nextKeyframeAction, GLOBAL_GO_TO_PREVIOUS_KEYFRAME: previousKeyframeAction,
        GLOBAL_SET_AUDIO_FROM_VIDEO: setAudioFromVideoAction, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: setAudioMarkFromVideoAction,
        GLOBAL_VIDEO_ZOOM: videoZoomAction, GLOBAL_RESET_VIDEO_ZOOM: resetVideoZoomAction,
        GLOBAL_VIDEO_RENDER_COST: renderCostAction,
        GLOBAL_OPEN_SUBS: openAction, GLOBAL_OPEN_VIDEO: openVideoAction, GLOBAL_OPEN_KEYFRAMES: openKeyframesAction,
        GLOBAL_OPEN_AUTO_SAVE: openAutoSaveAction,
        GLOBAL_OPEN_DUMMY_VIDEO: dummyVideoAction,
        GLOBAL_DELETE_TEMPORARY_FILES: removeTemporaryAction, GLOBAL_SETTINGS: settingsAction,
        GLOBAL_ABOUT: aboutAction, GLOBAL_HELPERS: creditsAction, GLOBAL_HELP: userGuideAction,
        GLOBAL_GETTING_STARTED: gettingStartedAction,
        GLOBAL_ANSI: reportIssueAction, GLOBAL_CHECK_FOR_UPDATES: checkForUpdatesAction,
        GLOBAL_EDITOR_HIDE_TAGS: hideEditorTagsAction,
        GLOBAL_TRANSLATION_MODE: translationModeAction,
        GLOBAL_CLOSE_PAGE: closeAction
    }
    }
    function globalItems() {
        return {
        GLOBAL_OPEN_SPELLCHECKER: checkSpellingItem, GLOBAL_OPEN_ASS_PROPERTIES: assPropertiesItem,
        GLOBAL_OPEN_STYLE_MANAGER: styleManagerItem, GLOBAL_OPEN_SUBS_RESAMPLE: resampleItem,
        GLOBAL_OPEN_FONT_COLLECTOR: fontCollectorItem,
        GLOBAL_SHOW_SHIFT_TIMES: showShiftTimesItem, GLOBAL_SHIFT_TIMES: runShiftTimesItem,
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
        const plain = text => {
            const tab = text.indexOf("\t")
            return (tab >= 0 ? text.slice(0, tab) : text).replace(/&(&?)/g, "$1")
        }
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
                const place = path.concat(plain(item.text)).join(" > ")
                if (!out[symbol])
                    out[symbol] = []
                if (!out[symbol].includes(place))
                    out[symbol].push(place)
            }
        }
        for (let i = 0; i < appMenus.count; ++i)
            walk(appMenus.menuAt(i), [plain(appMenus.menuAt(i).title)])
        walk(gridMenu, ["Grid menu"])
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
        const conversion = root.conversionItems.find(c => c[2] === symbol)
        if (conversion) {
            if (root.app.conversionTargets().indexOf(conversion[0]) >= 0)
                root.openConversion(conversion[0], conversion[1])
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
        case "GLOBAL_QUIT": root.close(); return true
        case "GLOBAL_PLAY_ACTUAL_LINE":
            if (root.video.hasVideo) {
                root.focusPanel(lineText, Qt.OtherFocusReason)
                root.video.playActualLine()
            }
            return true
        case "GLOBAL_STYLE_MANAGER_CLEAN_STYLE":
            if (editing)
                styleManagerWindow.deleteUnused()
            return true
        case "GLOBAL_JOIN_WITH_PREVIOUS": if (editing) root.app.joinLines("previous"); return true
        case "GLOBAL_JOIN_WITH_NEXT": if (editing) root.app.joinLines("next"); return true
        case "GLOBAL_NEXT_TAB": root.app.changeTab(1); return true
        case "GLOBAL_PREVIOUS_TAB": root.app.changeTab(-1); return true
        case "GLOBAL_REMOVE_LINES": if (editing) root.app.deleteLines(); return true
        case "GLOBAL_HIDE_TAGS": root.app.toggleHideTags(); return true
        case "GLOBAL_PREVIOUS_LINE": if (editing) root.editor.nextLine(-1); return true
        case "GLOBAL_NEXT_LINE": if (editing) root.editor.nextLine(1); return true
        case "GLOBAL_REMOVE_TEXT": if (editing) root.app.deleteText(); return true
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
        case "VIDEO_ASPECT_RATIO": aspectRatioDialog.openWindow(root.videoFullscreen.active ? videoFullscreenWindow : null); return true
        case "VIDEO_SAVE_FRAME_TO_PNG":
        case "VIDEO_COPY_FRAME_TO_CLIPBOARD":
        case "VIDEO_SAVE_SUBBED_FRAME_TO_PNG":
        case "VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD":
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
        case "GRID_COPY_COLUMNS": columnsWindow.choose(false); return true
        case "GRID_PASTE_COLUMNS": columnsWindow.choose(true); return true
        case "GRID_SET_NEW_FPS": root.openSetNewFps(); return true
        case "GRID_SET_FPS_FROM_VIDEO": if (video) root.app.setFpsFromVideo(); return true
        case "GRID_PASTE_TRANSLATION":
            if (root.app.canPasteTranslation()) {
                translationFileDialog.currentFolder = root.app.targetFolder()
                translationFileDialog.open()
            }
            return true
        case "GRID_TRANSLATION_DIALOG": if (root.app.canShiftTranslation()) translationShiftWindow.open(); return true
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
                enabled: !root.menuHasKeys && !root.workspaceLayout.modalOpen
                         && !root.hotkeys.macroTakenSequences.includes(modelData.keys)
                onActivated: root.automationHotkeys.run(modelData.legacyName)
            }
        }
    }
}
