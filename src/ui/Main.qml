pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Dialogs
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

// Classic shell (V1-S): menus across the top, video left, audio above the
// Line editor on the right, the Grid across the bottom and, when a protected
// comparison reference is open, its tray below. Panels are focus scopes, so
// F6/Shift+F6 traversal returns to whatever had focus inside a panel.
// Movable/floating panels and layout persistence arrive with the docking cards.
ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280
    height: 800
    visible: true
    title: shell.hasEditingTarget ? qsTr("%1 - HikariSub").arg(shell.editingTitle) : "HikariSub"
    color: Theme.background // K2: the application background between panels

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
    required property var whatsNew // SH3: WhatsNew
    required property var analytics // AN1 (#404): Analytics, anonymous usage data
    required property var styleManager
    required property var kanjiTimer // KA3: KanjiTimerController
    required property var fontCollector // Y8: FontCollectorController
    required property var fontCatalogs // Y6: FontCatalogsController
    required property var matroska // Y9: MatroskaController (GRID_SUBS_FROM_MKV)
    required property var hotkeys // O2: the shortcut editor (HotkeysController)
    required property var settingsImport // O3: SettingsImportController
    required property VisualToolsController visualTools // T1: the Video panel's visual tools
    required property VideoViewController videoView // V4: zoom, aspect, volume, snapshots
    required property StatusBarController statusBar // P10: the status bar's video and subtitle fields
    required property VideoFullscreenController videoFullscreen // V5: fullscreen and the monitors

    // Every registered macro, in load and registration order (the dynamic
    // part of the legacy Automation menu).
    readonly property var macroItems: {
        const out = []
        for (const script of root.automationManager.scripts)
            for (const macro of script.macros)
                out.push({ path: script.path, ordinal: macro.ordinal, name: macro.name })
        return out
    }

    // Subtitles > Conversion: target, label, GLOBAL_CONVERT_TO_* id.
    // P10: with legacy's help (HikariSubFrame.cpp:323-331).
    readonly property var conversionItems: [
        ["ass", qsTr("Convert to ASS"), "GLOBAL_CONVERT_TO_ASS", qsTr("Converts to ASS format")],
        ["srt", qsTr("Convert to SRT"), "GLOBAL_CONVERT_TO_SRT", qsTr("Converts to SRT format")],
        ["mdvd", qsTr("Convert to MDVD"), "GLOBAL_CONVERT_TO_MDVD", qsTr("Converts to microDVD format")],
        ["mpl2", qsTr("Convert to MPL2"), "GLOBAL_CONVERT_TO_MPL2", qsTr("Converts to MPL2 format")],
        ["tmp", qsTr("Convert to TMP"), "GLOBAL_CONVERT_TO_TMP", qsTr("Converts to TMPlayer format (not recommended)")]]

    // With their GLOBAL_SORT_ALL_BY_* / GLOBAL_SORT_SELECTED_BY_* ids and
    // legacy's help (P10, HikariSubFrame.cpp:227-234).
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

    // E2: a custom tag button. Types 0 and 1 work in the focused field like
    // Bold; plain text (type 2) goes, as in legacy, into the main field (the
    // translation in translation mode, unless it is empty).
    // `edited`: the text field edited before a menu took the focus (LE1's
    // More), else the one edited now.
    function applyTagButton(index, edited) {
        // EditBox::OnButtonTag reads the button's option as stored now.
        const b = root.tagButtons.pressed(index)
        if (b.tag === undefined || b.tag.length === 0)
            return false
        let field = edited ?? root.editedField()
        if (b.type === 2)
            field = root.editor.translationMode && translationText.text.length > 0 ? translationText : lineText
        return root.editor.applyTagButton(field.role, b.tag, b.type, field.selectionStart, field.selectionEnd)
    }

    // Close review (P1): rows of Documents with unsaved work, then `then`.
    function beginClose(then) {
        const rows = root.app.reviewClose(then)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    // Opening subtitles into the editing target (P2): staged first, then the
    // target's unsaved work is reviewed as for Close.
    function openSubtitles(path) {
        const result = root.app.reviewOpen(path)
        if (!result.ok || result.done)
            return // the log window shows the problem; P9: opened in a new tab
        else if (result.rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(result.rows)
    }
    // File > Open (GLOBAL_OPEN_SUBS, HikariSubFrame.cpp:1008-1033 at
    // 20d647c4): an MKV file goes to OnMkvSubs, its subtitle track loaded as
    // GRID_SUBS_FROM_MKV loads the tab video's (in a new tab when the tab has
    // subtitles of a file); an OGM file opens as OpenFile opens a video;
    // anything else opens as subtitles.
    function openChosenSubtitles(path) {
        if (root.matroska.opensAsMatroska(path))
            matroskaSubtitles.beginFile(path)
        else if (/\.ogm$/i.test(path))
            tabCommands.openVideoFile(path)
        else
            root.openSubtitles(path)
    }
    // Legacy ReloadSubsIfModified when the window becomes active (and
    // ChangePage's on a tab change): a bar on that Document instead of a
    // modal question (PU1-disk-change-bar). Documents whose bar shows, by id.
    property var changedOnDisk: ({})
    // Documents saved under another name while their bar showed: the bar
    // goes once that write is done (a failed one leaves it).
    property var savingAway: ({})
    // PU1-mkv-notices: the tab an MKV load started on (taken as it starts,
    // before its tracks are listed, as the application takes its target);
    // "home" for a load File > Open started on Home.
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
    // The close as the window receives it, before the docking engine closes
    // every panel (Docking.guardClose, from the first frame on): the review
    // of unsaved work holds a close back here, so the panels stay while it is
    // shown and after it is cancelled, and a close that goes ahead saves the
    // layout as the user sees it, the last save of the session.
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
        // HikariSubFrame::OnClose: FR->SaveOptions().
        searchTool.save()
        root.app.endSession() // P6: SaveLastSession(true)
    }
    // P10: the menus' help in the status bar's first field (StatusHelp).
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
        // F1: a file result opens into the changed Untitled Document.
        function onFindOpenReview(rows) {
            closeReview.review(rows)
        }
        // DestroyDialogs (a changed program font): the Search tool saves its
        // tab (FR->SaveOptions) and closes with its results.
        function onFindReplaceDestroyed() {
            searchTool.destroyTool()
            searchDock.close()
        }
    }

    // D1: the Classic arrangement (also Reset layout): Video beside Audio over
    // the Line editor, the Grid below, the Reference under it when there is one.
    // Legacy AUDIO_BOX_HEIGHT: the display with its ruler, the search bar
    // and the button row take 170 px; the Audio dock's request in
    // defaultLayout() assumes a chrome height (title bars) that fonts change,
    // so the difference is moved across the dock's bottom edge.
    function fitAudioBox() {
        // With audio open the box fills the panel's body (the search bar and
        // button rows appear then), so the body is what gets 170 px: the
        // group's height in the layout less its header (35) and the body's
        // margins.
        const chrome = audioDock.isOpen ? Math.round(root.workspaceLayout.panelSize(audioDock).height - audioPanel.bodyHeight) : 0
        // D3: the Line editor below keeps its minimum (Panel). When it has
        // no room to give, the first move of the separator between them
        // grows the top row instead (the engine takes the room from the
        // Grid's row and gives it to the editor), and the next one gives it
        // to the audio box. The default arrangement shows the editor's rows
        // whole, so while it is fitted that minimum is their height (with
        // the body's margins); a dock squeezed later scrolls them.
        Docking.setMinimumSize("Editor", Qt.size(editorPanel.minimumSize.width,
                                                 Math.ceil(editorColumn.implicitHeight) + 8))
        for (let pass = 0; pass < 3; ++pass) {
            const body = root.workspaceLayout.panelSize(audioDock).height - chrome
            const deficit = Math.round(170 - body)
            if (deficit === 0 || !audioDock.isOpen)
                break
            Docking.resizeInLayout("Audio", 0, 0, 0, deficit)
        }
        editorPanel.reportMinimumSize()
    }

    function defaultLayout() {
        // Search (its scope rail beside the results) spans the bottom; it is
        // placed and closed first so the panels after it share the whole area.
        dockingArea.addDockWidget(searchDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 280))
        searchDock.close()
        dockingArea.addDockWidget(gridDock, KDDW.KDDockWidgets.Location_OnBottom)
        // The top row and the Audio dock are sized so that at the default
        // window (1280 x 800) the audio box gets legacy's AUDIO_BOX_HEIGHT
        // (170 px for the display, search bar and buttons) above a usable
        // Line editor; the docking engine shares the row by these requests.
        dockingArea.addDockWidget(videoDock, KDDW.KDDockWidgets.Location_OnTop, gridDock, Qt.size(640, 520))
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnRight, videoDock)
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 272))
        // TM1: the Original text panel's place, between the Audio panel and
        // the Line editor; it shows while the mode is on (placeOriginal).
        dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        originalDock.close()
        dockingArea.addDockWidget(referenceDock, KDDW.KDDockWidgets.Location_OnBottom, gridDock, Qt.size(0, 160))
        if (!root.shell.hasReference)
            referenceDock.close()
        // Tools start closed (docs/qt/ux/workspaces.md); Timing,
        // Automation and Styles tab with the editor.
        editorDock.addDockWidgetAsTab(timingDock)
        editorDock.addDockWidgetAsTab(automationDock)
        editorDock.addDockWidgetAsTab(stylesDock)
        // KE2: the Template editor tabs with the editor too.
        editorDock.addDockWidgetAsTab(templateDock)
        editorDock.setAsCurrentTab()
        timingDock.close()
        automationDock.close()
        stylesDock.close()
        templateDock.close()
        // NT1: the Log has no place until it first opens (addLog), so the
        // arrangement keeps the sizes it had without it.
        // PU2: Fix minor errors at the right edge, full height, 420 wide.
        dockingArea.addDockWidget(misspellDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(420, 0))
        misspellDock.close()
    }

    // TP1 (#274): the Typesetting preset's own arrangement (owner-approved
    // mockup 2026-10-22), built from the default one the first time the
    // preset is chosen and kept as the preset's (buildsArrangement). The
    // Video panel leads the upper row, about two thirds of the height, with
    // Styles on its right in about a quarter of the width; below, the Line
    // editor at its natural height beside a short Grid. Audio is closed; it and the Original text panel
    // keep their places above the Line editor, as in Editing.
    function typesettingLayout() {
        const area = root.workspaceLayout.panelSize(gridDock).width
        // The Line editor leaves the right column for the Grid's row, on
        // its left; Audio and the Original text go above it (shown for the
        // move, then closed again).
        dockingArea.addDockWidget(editorDock, KDDW.KDDockWidgets.Location_OnLeft, gridDock,
                                  Qt.size(Math.round(area * 0.44), 0))
        // LE1: wide enough for the Line editor's two rows of fields (narrow,
        // their names go above them and the rows double), up to half.
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
        // Wide enough for its header's commands whatever the font (the
        // title's width varies), with a little room to spare.
        const tools = stylesTool.toolbar
        const short = Math.ceil(tools.implicitWidth + 12 - tools.width)
        if (tools.visible && short > 0)
            Docking.resizeInLayout("Styles", short, 0, 0, 0)
        // The lower row at the Line editor's natural height at its new
        // width (its rows whole, as fitAudioBox measures them, below its
        // header), the rest to the upper row. The Video panel's bottom edge
        // moves: the Line editor's top edge is the closed Audio and Original
        // places', which the engine doesn't move.
        editorColumn.ensurePolished()
        const lower = root.workspaceLayout.panelSize(editorDock).height
        const natural = Math.ceil(editorColumn.implicitHeight) + 8 + lower - editorPanel.height
        if (lower !== natural)
            Docking.resizeInLayout("Video", 0, 0, 0, lower - natural)
    }
    // KP1 (#286): the Karaoke preset's own arrangement (owner-approved
    // mockup A, 2026-10-26), built from the default one as Typesetting's is.
    // Video and the Template editor, the Line editor the tab behind it, share
    // the upper row; Audio spans the window under them, so a Line's syllables
    // get its whole width; a short Grid at the bottom.
    function karaokeLayout() {
        // Audio leaves the Line editor's column for the Grid's row, above it,
        // at the height Editing gives it.
        dockingArea.addDockWidget(audioDock, KDDW.KDDockWidgets.Location_OnTop, gridDock,
                                  Qt.size(0, root.workspaceLayout.panelSize(audioDock).height))
        templateDock.open()
        templateDock.raise()
        // The Grid gets its heading and seven rows; Audio, above it, gives or
        // takes the difference.
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
        // The tray is part of the Grid: a comparison opened while the
        // tab's layout has the Grid closed leaves it closed too.
        function onHasReferenceChanged() {
            if (root.shell.hasReference && gridDock.isOpen)
                referenceDock.open()
            else if (referenceDock.isOpen)
                referenceDock.close()
        }
    }

    // TM1 (#232): the Original text panel follows the editing target's
    // Translation mode, as the Reference tray follows the comparison: while
    // the mode is on it opens where the arrangement keeps its place (above
    // the Line editor in one that never had it), and it closes when the mode
    // goes off. Closed by hand, it stays closed until the mode or the
    // arrangement changes, and the Line editor shows the Original text then.
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
    // Where an arrangement that never had the panel (an older saved layout)
    // puts it: above the Line editor, in the Line editor's own window when
    // that floats (placePanel), or on the right without one.
    function addOriginal() {
        if (editorDock.isOpen && editorDock.isFloating)
            editorDock.addDockWidgetToContainingWindow(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock,
                                                       Qt.size(0, 140))
        else if (editorDock.isOpen)
            dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnTop, editorDock, Qt.size(0, 140))
        else
            dockingArea.addDockWidget(originalDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(400, 0))
    }

    // NT1 (#302): the Log's place when it opens in an arrangement that has
    // none for it (every preset, an older saved layout): along the bottom,
    // the whole width.
    function addLog() {
        dockingArea.addDockWidget(logDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 180))
    }
    // PU6 (#308): the Spellchecker panel's place the same way (every preset,
    // an arrangement saved before it existed: panel registry 9 or less),
    // along the bottom, the whole width.
    function addSpellChecker() {
        dockingArea.addDockWidget(spellDock, KDDW.KDDockWidgets.Location_OnBottom, null, Qt.size(0, 180))
    }
    // Subtitles > Check spelling (GLOBAL_OPEN_SPELLCHECKER): the panel,
    // checking from the active Line, again when it was already checking.
    function openSpellChecker() {
        const checking = spellTool.checking
        root.showPanel(spellDock)
        if (checking)
            spellTool.start()
    }

    // TP1 (#274): the Styles panel's place when it opens in an arrangement
    // saved before it existed (panel registry 6 or earlier): its default
    // one, a tab with the Line editor, or the window's right side when the
    // Line editor is closed.
    function addStyles() {
        if (editorDock.isOpen)
            editorDock.addDockWidgetAsTab(stylesDock)
        else
            dockingArea.addDockWidget(stylesDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(300, 0))
    }
    // KE2: the Template editor's, in one saved before it (registry 8 or
    // earlier), the same way.
    function addTemplate() {
        if (editorDock.isOpen)
            editorDock.addDockWidgetAsTab(templateDock)
        else
            dockingArea.addDockWidget(templateDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(300, 0))
    }

    // AD3 (#312): a run shows on its progress card (AutomationProgress, in
    // the toast stack); the Automation panel no longer opens for it. The
    // card's "Open in panel" shows the run in the panel's Runs.
    function openAutomationRun(runId) {
        if (root.videoFullscreen.active)
            root.videoFullscreen.leave()
        root.showPanel(automationDock)
        automationTool.showRun(runId)
    }

    // Major panels in F6 order; a hidden panel is skipped.
    readonly property list<Item> panels: [videoPanel, audioPanel, originalPanel, editorPanel, gridPanel, referencePanel,
                                          timingPanel, searchPanel, automationPanel, spellPanel, logPanel, stylesPanel,
                                          misspellPanel, templatePanel]
    // D1: their docks, in the same order.
    readonly property var dockList: [videoDock, audioDock, originalDock, editorDock, gridDock, referenceDock, timingDock,
                                     searchDock, automationDock, spellDock, logDock, stylesDock, misspellDock, templateDock]
    // KB2, KB1 (key_router.h): a panel's bindings act before the control
    // it has focused (a clicked button keeps no Space of its own), on the
    // hovered panel rather than the focused one, and the Video window's
    // VIDEO_PLAY_PAUSE (Space by default) acts from every panel; never while
    // a text field or a control the keyboard moved to has focus.
    KeyRouter {
        id: keyRouter
        objectName: "keyRouter"
        fallbackTarget: videoPanel
        fallbackAccepts: (key, modifiers) => root.hotkeys.actionFor(3, key, modifiers) === "VIDEO_PLAY_PAUSE"
        // AUDIO_AUTO_FOCUS folded in: legacy's audio took the focus under
        // the pointer; now no panel does, and the hotkeys follow it. KA2:
        // not while tap mode is on, so its taps stay the audio panel's.
        followsPointer: () => root.app.settings.value("audio.autoFocus") && !root.audio.tapOn
        Component.onCompleted: {
            for (const panel of root.panels)
                keyRouter.addPanel(panel, panel.keyTarget)
        }
    }

    // A preset is the Editing arrangement with the panels it leaves closed
    // (Timing: Video; Translation and Typesetting: Audio), for the current
    // Document tab only (WS1).
    function applyPreset(name) {
        // Before the first frame the default arrangement and the saved
        // layout are not in place yet: settle them first.
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applyPreset(name))
        root.analytics.layoutChanged(name) // AN1 (#404)
    }
    // WS3: a saved layout for the current tab, and Reset this layout (the
    // saved layout the tab's arrangement started from, else its preset).
    function applySavedLayout(name) {
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.applySavedLayout(name))
        root.analytics.layoutChanged("saved") // AN1 (#404): never the layout's name
    }
    function resetLayout() {
        root.settleArrangement()
        root.keepFocusShown(() => root.workspaceLayout.resetCurrentLayout())
    }
    // A panel with the focus that the new layout hides gives it to the
    // Grid, else to the first shown panel in F6 order (as the retired
    // arrangements did).
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
    // WS1: another arrangement is shown (another tab's, a preset, a saved
    // layout, the backup). The Reference tray follows the comparison, not
    // the arrangement (shown with the Grid while there is one), and a video
    // the arrangement hides pauses (no transport shows it playing).
    Connections {
        target: root.workspaceLayout
        function onArrangementSwapped() {
            if (root.shell.hasReference && gridDock.isOpen)
                referenceDock.open()
            else if (referenceDock.isOpen)
                referenceDock.close()
            if (!videoDock.isOpen && root.video.playing)
                root.video.pause()
            // TM1: once the editor is the shown tab's: a tab's arrangement
            // is swapped in before the editor leaves the tab before.
            Qt.callLater(root.placeOriginal)
        }
        // TM1: a preset that carries a mode (WorkspaceLayoutController::presetMode).
        // Translation asks to turn the editing target's Translation mode on
        // and leaves it on when another arrangement is chosen.
        function onPresetModeChosen(mode) {
            if (mode === "Translation" && root.app.translationQuestionDue())
                root.openInMainWindow(translationModeQuestion)
            if (mode === "Karaoke") {
                // KP1: Karaoke mode and the Grid's folding on, remembered as
                // Karaoke's own change while nobody changes them after.
                const karaoke = root.audio.karaoke
                const folds = root.shell.foldGenerated
                root.audio.setKaraoke(true)
                root.shell.foldGenerated = true
                root.karaokeTurnedOn = root.karaokeTurnedOn || !karaoke
                root.foldTurnedOn = root.foldTurnedOn || !folds
            }
        }
        // KP1: leaving Karaoke puts back what it changed itself.
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
    // KP1: what choosing Karaoke turned on; a change by anyone else since
    // keeps it as it is when Karaoke is left.
    property bool karaokeTurnedOn: false
    property bool foldTurnedOn: false
    readonly property bool audioKaraoke: root.audio.karaoke
    readonly property bool gridFolds: root.shell.foldGenerated
    onAudioKaraokeChanged: root.karaokeTurnedOn = false
    onGridFoldsChanged: root.foldTurnedOn = false

    // F1: GLOBAL_SEARCH (0) and GLOBAL_FIND_REPLACE (1) open the Search tool
    // on that tab; the same shortcut again while the tool has the focus on
    // that tab hides it (legacy ShowDialog). The focus coming in from
    // elsewhere takes the editor's selection (legacy OnActivate).
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

    // GLOBAL_SHIFT_TIMES: shifting only start or end times asks first
    // (legacy; PU4-shift-question: PK1's message box, which can be hidden).
    // The answer shifts the document it was asked for: another editing
    // target meanwhile (the main window stays in use under the fullscreen
    // video window's question) takes the question back.
    function runShiftTimes() {
        const which = root.shiftTimes.settings.whichTimes
        const pp = root.shiftTimes.settings.postprocessor
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

    // The dock of the panel with the focus (the Grid's otherwise).
    function focusedDock() {
        const w = root.workspaceLayout.focusWindow
        const i = panels.findIndex(p => p.activeFocus && p.Window.window === w)
        return i >= 0 ? dockList[i] : gridDock
    }

    // D1 keyboard placement: kind 0 tab with, 1 left of, 2 above, 3 right of,
    // 4 below, 5 float. The moved panel keeps the focus.
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

    // D1 Move before/after: the panel `step` places along its group's tabs;
    // it stays the shown tab and keeps the focus.
    function moveTab(dock, step) {
        if (!Docking.moveTab(dock.uniqueName, step))
            return false
        root.showPanel(dock)
        return true
    }

    // View > Panels > Show: open the panel, bring its window up and focus it.
    // GLOBAL_EDITOR's VideoBox::OpenEditor (fullscreen's Open editor): out
    // of fullscreen to the Line editor, shown even where the tab's layout
    // has it closed (WS3).
    function openEditorFromFullScreen() {
        if (root.app.openEditorFromFullScreen())
            root.showPanel(editorDock)
    }
    // TM1: the Original text panel has no content outside translation mode,
    // so nothing opens it then.
    function panelAvailable(dock) {
        return dock !== originalDock || root.editor.translationMode
    }
    // Edit › Fix minor errors shows or hides its panel (legacy
    // MR->Show(!MR->IsShown())).
    function toggleMisspellPanel() {
        if (misspellDock.isOpen)
            misspellDock.close()
        else
            root.showPanel(misspellDock)
    }
    function showPanel(dock) {
        if (!root.panelAvailable(dock))
            return
        root.analytics.panelOpened(dock.uniqueName) // AN1 (#404): once per panel and session
        if (dock === originalDock)
            root.openOriginal()
        else if (dock === logDock && !Docking.hasPlace("Log"))
            root.addLog()
        else if (dock === spellDock && !Docking.hasPlace("Spellchecker"))
            root.addSpellChecker()
        else if (dock === stylesDock && !Docking.hasPlace("Styles"))
            root.addStyles()
        else if (dock === misspellDock && !Docking.hasPlace("FixMinorErrors"))
            // An arrangement saved before the panel (registry 7 or less) has no
            // place for it: the right edge, as the Classic arrangement.
            dockingArea.addDockWidget(misspellDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(420, 0))
        else if (dock === templateDock && !Docking.hasPlace("Template"))
            root.addTemplate()
        else
            dock.open()
        root.workspaceLayout.keepFloatingPanelsOnScreen()
        dock.raise()
        // PU6: showing the Spellchecker starts it at the active Line.
        if (dock === spellDock && !spellTool.checking)
            spellTool.start()
        root.focusPanel(panels[dockList.indexOf(dock)], Qt.OtherFocusReason)
    }

    // The panel an item belongs to; a dock header (DockTabBar.qml) belongs
    // to its current panel.
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

    // DX1 (#367): User guide (GLOBAL_HELP): the page of the DG1 window with
    // the keyboard (its guideId), else of the panel the hotkeys act on (the
    // one under the pointer while they follow it, KeyRouter, else the one
    // with the focus); the guide's home for anything else.
    function openUserGuide() {
        const w = root.workspaceLayout.focusWindow
        if (w && w.guideId !== undefined) {
            UserGuide.open("window", w.guideId)
            return
        }
        const hovered = keyRouter.followsPointer() ? keyRouter.hoveredPanel : null
        const panel = hovered || root.panelOf(w ? w.activeFocusItem : root.activeFocusItem)
        const i = panel ? root.panels.indexOf(panel) : -1
        UserGuide.open(i >= 0 ? "panel" : "", i >= 0 ? root.dockList[i].uniqueName : "")
    }
    Connections {
        target: UserGuide
        function onOpenRequested(kind, id) { root.app.openUserGuide(kind, id) }
    }
    // User guide's keys in a DG1 window, where the Global bindings are off.
    Shortcut {
        objectName: "dialogUserGuide"
        sequences: root.hotkeys.globalShortcuts.filter(s => s.symbol === "GLOBAL_HELP").map(s => s.keys)
        context: Qt.ApplicationShortcut
        enabled: !root.shellActive && root.workspaceLayout.focusWindow !== null
                 && root.workspaceLayout.focusWindow.guideId !== undefined
        onActivated: root.openUserGuide()
    }

    // D3: a panel's "⋯" menu (MuseScore's DockPanelMenuModel,
    // docs/research/musescore-docking.md §1): the panel's own items, then
    // Move panel…, Undock (Dock while it floats) and Close, each disabled
    // where the panel cannot do it (its dock's options: not closable, not
    // dockable). Its header asks for it (Docking.requestMenu: the "⋯"
    // button, a right click on the header, Space or Enter on the button).
    // A panel's own items are commands that have no place in its body or
    // its header's toolbar, never a copy of them. The Reference tray has
    // "Compare with reference" (R1-compare-reference: the editing target
    // with the Protected reference, which the tab menu's Compare cannot
    // reach when the reference is no tab). Its Close closes the reference,
    // the tray with it (legacy's close mark, DestroyPreview), so the header
    // holds one close, not two.
    // An item's `enabled` is a function its entry follows, so the list itself
    // stays put while the menu is open (a new list would remake the entries,
    // the one being chosen too).
    // LE2: the Video panel's Times/Frames choice (legacy's radios under the
    // Line editor, EDITBOX_TIMES_TO_FRAMES_SWITCH), enabled once a video was
    // opened; the Line editor and the Grid follow it.
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
        // AM1: the Automation menu's commands for scripts and the Hotkeys
        // page on the macros' shortcuts (a legacy Hotkeys.txt is imported
        // there since PU7, #309).
        if (dock === automationDock)
            return [{ name: "automationLoadScript", text: qsTranslate("HikariSub.Automation", "Load script…"), iconRole: "automation",
                      enabled: () => loadScriptAction.enabled, run: () => loadScriptAction.trigger() },
                    { name: "automationRefreshAutoload", text: qsTranslate("HikariSub.Automation", "Refresh autoload scripts"),
                      iconRole: "refresh", enabled: () => reloadAutoloadAction.enabled && !root.automation.running,
                      run: () => reloadAutoloadAction.trigger() },
                    { name: "automationShortcutsInSettings", text: qsTranslate("HikariSub.Automation", "Shortcuts in Settings…"),
                      iconRole: "hotkeys", run: () => root.openSettings("hotkeys", 7) },
                    // AD3: the finished runs go from Runs.
                    { name: "automationClearRuns", text: qsTranslate("HikariSub.Automation", "Clear runs"),
                      iconRole: "remove", enabled: () => root.automation.runs.count > (root.automation.running ? 1 : 0),
                      run: () => root.automation.runs.clear() }]
        // PU6: the Spellchecker's two options (legacy's check boxes) and
        // the user dictionary's words (legacy's "Remove from dictionary").
        if (dock === spellDock)
            return [{ name: "spellIgnoreComments", text: qsTranslate("HikariSub.Spelling", "Ignore comments"), checkable: true,
                      checked: () => spellTool.ignoreComments, run: () => spellTool.setIgnoreComments(!spellTool.ignoreComments) },
                    { name: "spellIgnoreUpper", text: qsTranslate("HikariSub.Spelling", "Ignore words in capitals"), checkable: true,
                      checked: () => spellTool.ignoreUpperCase,
                      run: () => spellTool.setIgnoreUpperCase(!spellTool.ignoreUpperCase) },
                    { name: "spellAddedWords", text: qsTranslate("HikariSub.Spelling", "Words added to dictionary…"),
                      iconRole: "dictionary", run: () => spellTool.openAddedWords() }]
        // KP1 (#286): runs of generated Lines folded into one row each.
        if (dock === gridDock)
            return [{ name: "foldGenerated", text: qsTranslate("HikariSub.Grid", "Fold generated lines"), checkable: true,
                      checked: () => root.shell.foldGenerated,
                      run: () => root.shell.foldGenerated = !root.shell.foldGenerated }]
        return []
    }
    // VF1 (#288): a panel's own submenus, after its own items: an item's
    // `items` are radio choices, its `iconRole` the submenu's. The Video
    // panel's "Seek on" and "Then play" (legacy VideoToolbar's two lists,
    // which left the panel).
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
        // The panel's place among its group's tabs, read as the menu opens.
        property int tabIndex: -1
        property int tabCount: 0
        // Left is toward the first tab, unless the layout is mirrored.
        readonly property int leftStep: Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1
        function openFor(target, anchor) {
            dock = target
            tabIndex = Docking.tabIndex(target.uniqueName)
            tabCount = Docking.tabCount(target.uniqueName)
            // inside a floating panel's frame, not over its drawn shadow
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
                // A choice among items (radio) closes the menu, as legacy's ITEM_RADIO.
                keepsMenuOpen: checkable && modelData.radio !== true
                enabled: typeof modelData.enabled === "function" ? modelData.enabled() : modelData.enabled !== false
                onTriggered: {
                    modelData.run()
                    // The item's check follows its state, not the click's toggle.
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
                        keepsMenuOpen: false // a choice among items closes the menu (ITEM_RADIO)
                        onTriggered: {
                            modelData.run()
                            checked = Qt.binding(() => modelData.checked())
                        }
                    }
                    onObjectAdded: (index, object) => ownMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => ownMenu.removeItem(object)
                }
            }
            // After the own items, whichever Instantiator fills first.
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
        // D1 Move before/after: a tab one place along its tab bar, kept
        // current and focused; only in a group of tabs, off at its end.
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
        if (homePage.visible) // SH2: the Home page is the one region
            return homePage.takeFocus()
        const shown = panels.filter(p => p.visible)
        // AD3: the progress card is a region after the panels while it
        // shows its controls (F6 never starts on it: it takes no focus by
        // itself).
        const card = automationProgress.reachable ? automationProgress : null
        if (card && card.activeFocus) {
            card.leave()
            if (shown.length > 0)
                root.focusPanel(shown[step > 0 ? 0 : shown.length - 1], Qt.TabFocusReason)
            return
        }
        // Every panel closed: the card is the one region, or there is none.
        if (shown.length === 0) {
            if (card)
                root.enterCard(null)
            return
        }
        // The panel with focus in the focus window: the main one or a
        // floating panel group (D1). Window.active cannot tell them apart:
        // Qt reports a floating panel's window active whenever its transient
        // parent, the main window, is, so the shell compares the
        // application's focus window.
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

    // The panel waiting for its window's activation (focusPanel), or what
    // takes the focus then ({window, focus}: enterCard).
    property var pendingFocus: null

    // AD3: F6 into the progress card, whose window (the main one, or the
    // fullscreen video's) is activated first when the keys are elsewhere,
    // as focusPanel does for a panel.
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

    // TM1: a question of the main window's (a dialog in its overlay) asked
    // from a floating panel's window, the Translation mode hotkey's say: the
    // main window comes up and is activated first, so the question shows
    // above that window and takes the keys.
    function openInMainWindow(dialog) {
        if (root.workspaceLayout.focusWindow !== root) {
            root.raise()
            root.requestActivate()
        }
        dialog.open()
    }

    // TM1: the text field the Line editor's commands act on. With the
    // Original text panel in a window of its own each window keeps its own
    // focus item, so the Translated text gets its active focus back whenever
    // the main window is activated, by a click on a tag button too (an
    // activation or a popup's closing, as the platform reports it); the
    // field last focused for any other reason is the one being edited.
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
                // The main window activated with nothing to focus (its focused
                // panel floated away): the next visible region takes it, the
                // Grid first (docs/qt/docking.md, focus restoration).
                const w = root.workspaceLayout.focusWindow
                const a = root.activeFocusItem // nothing, or the window's own root items
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
            // Again after the other activation handlers (the docking layer
            // focuses its own frame when a floating window activates).
            Qt.callLater(take)
        }
    }

    // Focus a panel, activating its window first when that is not the focus
    // window (a floating panel, or the main window from one). Activation is
    // asynchronous on X11 and Wayland, and the window gives the focus to its
    // own item when it activates, so the panel takes it again then. A panel
    // behind another tab of its group (TM1: the Original text and the Line
    // editor tabbed together) comes to the front first.
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

    // D1: Float and Dock from View > Panels (and Move panel's Float) leave the
    // focus on the moved panel, in its new window (docs/qt/docking.md, focus
    // restoration). The panel's window changes when the engine reparents it.
    function setPanelFloating(dock, floating) {
        const panel = panels[dockList.indexOf(dock)]
        dock.isFloating = floating
        Qt.callLater(() => root.focusPanel(panel, Qt.OtherFocusReason))
    }

    // V5: a double click on the picture (VideoBox.cpp:554-559): SetFullscreen(),
    // and once out of fullscreen the Line shown at the video's time with
    // GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN.
    function videoDoubleClick() {
        root.videoFullscreen.toggle(0)
        if (!root.videoFullscreen.active)
            root.app.selectVisibleLineAfterFullScreen()
    }
    // V5: OpenFile(path, fulls) and OpenFile in fullscreen: subtitles named
    // as the video load without "Load subtitles named ...?" (asked only
    // `!fulls && !IsFullScreen()`, HikariSubFrame.cpp:1351), then the video.
    // `fullscreen` (V5): legacy OpenFile's `fulls`, for that open only.
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
    // V5: OpenFiles with one file, a video: OpenFile(files[0], videos.size()
    // == 1 && VIDEO_FULL_SCREEN_ON_START) (HikariSubFrame.cpp:1856-1858).
    function openSingleVideo(path) {
        if (root.app.settings.value("video.fullScreenOnStart")) {
            root.openVideoUnasked(path, true)
        } else {
            tabCommands.openVideoFile(path)
        }
    }

    // D1: a floating panel's window is part of the shell. The Classic
    // shortcuts work there too, and not in dialogs or tool windows.
    readonly property bool floatingPanelActive: {
        const w = root.workspaceLayout.focusWindow
        return w !== null && w !== root && panels.some(p => p.visible && p.Window.window === w)
    }
    // While a menu has the keyboard none of the shell's shortcuts fire, as
    // legacy's menus took every key while shown (MenuBar::OnKey, Menu.cpp:1335):
    // Left and Right (Previous and Next frame) open and close submenus.
    readonly property bool menuHasKeys: root.workspaceLayout.menuHasFocus
    // V5: the fullscreen video window takes the Global bindings too
    // (Fullscreen::SetAccels, VideoFullscreen.cpp:222-260).
    readonly property bool videoFullscreenActive: root.workspaceLayout.focusWindow !== null
                                                  && root.workspaceLayout.focusWindow === videoFullscreenWindow
    readonly property bool shellActive: (root.workspaceLayout.focusWindow === root || floatingPanelActive
                                         || videoFullscreenActive) && !menuHasKeys
    // O2: the Global window's accelerator table (HikariSubFrame::SetAccels,
    // HikariSubFrame.cpp:1754-1800), on the whole shell (legacy's Tabs). The
    // focused panel's own bindings come first (each panel takes its keys in
    // ShortcutOverride, as the nearer wx accelerator table wins), then these.
    // Every Global binding is routed here, the menu items' included, so
    // remapping any of them works (runGlobalHotkey).
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
    // Application-wide, so F6 also leaves a floating panel group (D1); a
    // Global binding of the same keys comes first. Off in a dialog's window
    // (the Style manager's F6 goes from list to list, SY1).
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
    // Windows hands Shift+F10 to the system when no shortcut wants it; the
    // system then asks for a context menu but also puts the window in its
    // menu mode, which takes the next keys (native gate,
    // header-menu-undock-dock-keyboard). Claimed here, the key reaches the
    // focused control first (a panel header takes it); elsewhere it asks the
    // focused control for its context menu, as Windows would.
    Shortcut {
        sequences: ["Shift+F10"]
        context: Qt.ApplicationShortcut
        enabled: Qt.platform.os === "windows" && !root.hotkeys.globalSequences.includes("Shift+F10")
        onActivated: Docking.requestKeyboardContextMenu()
    }
    // P6: a tab closed from the tab bar (its close mark or a middle click).
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
        // P6: the shown tab's Grid scroll (legacy each tab kept its Grid), and
        // legacy ChangePage's ReloadSubsIfModified.
        // The application ignores the Grid's scroll reports from the tab
        // change until scrollRestored(), so the model reset's transient 0
        // never replaces the tab's own scroll.
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
        function onTabShown(scroll) {
            Qt.callLater(() => {
                grid.contentY = scroll * grid.rowHeight
                root.app.scrollRestored()
                root.checkExternalChange()
            })
        }
    }

    // SH1 (#220): the window's frame and its title bar (MainTitleBar.qml,
    // window_frame.h): HikariSub's own (frameless, the menus, the layout
    // switcher, the title and the window buttons in one row) or the
    // system's, by the "Use system title bar" setting (appearance.titleBar:
    // "auto" is the desktop's default, WindowFrame.systemTitleBarByDefault).
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
        // (customAvailable is read for its notification: the desktop can change in tests.)
        custom: WindowFrame.customAvailable && !WindowFrame.usesSystemTitleBar(root.titleBarChoice)
    }
    menuBar: MainTitleBar {
        id: titleBar
        frame: windowFrame
        menus: appMenus
        // WS3: the layout switcher after the menus, where MuseScore 4 puts
        // its Workspaces button.
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
    // The resize band and boundary of the frameless window (Wayland, X11),
    // over everything but the popups.
    MainWindowEdges {
        frame: windowFrame
        parent: root.contentItem.parent
        anchors.fill: parent
        z: 100
    }
    // SH1: F10 puts the keyboard on the menus (MS1 §1 "Menu bar"), as Alt
    // alone does (MainWindowFrame.altTapped); a Global binding of F10 comes
    // first.
    Shortcut {
        objectName: "menusShortcut"
        sequences: ["F10"]
        enabled: !root.menuHasKeys && !root.hotkeys.globalSequences.includes("F10")
        onActivated: titleBar.keyboardMode ? titleBar.leaveKeyboard(true) : titleBar.enterKeyboard()
    }

    // Classic menus, in the title bar. Commands join through the shared
    // action system as their cards land. K1: the items legacy drew with a
    // bitmap (HikariSubFrame.cpp's AppendTool) show the set's icon
    // (ShellMenuItem, ShellMenu).
    TitleBarMenus {
        id: appMenus
        TitleBarMenuItem {
            objectName: "fileMenuBarItem"
            menu: ShellMenu {
                title: qsTr("&File")
                ShellMenuItem {
                    command: "GLOBAL_OPEN_SUBS"
                    help: qsTr("Open subtitle file") // HikariSubFrame.cpp:178
                    iconRole: "open-subtitles"
                    action: Action {
                        id: openAction
                        text: qsTr("&Open…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS")) openDialog.show()
                    }
                }
                ShellMenu {
                    help: qsTr("Recently opened subtitles") // HikariSubFrame.cpp:188
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
                            help: qsTr("Open") + " " + modelData.path // HikariSubFrame.cpp:1574
                            text: modelData.label
                            // P9: Ctrl+click shows the file in its folder.
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
                    help: qsTr("Remove subtitles from the editor") // HikariSubFrame.cpp:190
                    iconRole: "close-subtitles"
                    objectName: "newMenuItem"
                    // Legacy GLOBAL_REMOVE_SUBS: the tab gets an Untitled default Document.
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
                    help: qsTr("Opens video file") // HikariSubFrame.cpp:247
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
                    help: qsTr("Save current file") // HikariSubFrame.cpp:180
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
                    help: qsTr("Save all subtitles") // HikariSubFrame.cpp:182
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
                    help: qsTr("Save as") // HikariSubFrame.cpp:184
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
                    help: qsTr("Save translation") // HikariSubFrame.cpp:186
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
                    help: qsTr("Save subtitles using the video name") // HikariSubFrame.cpp:192
                    iconRole: "save-with-video-name"
                    objectName: "saveWithVideoNameMenuItem"
                    action: Action {
                        id: saveWithVideoNameAction
                        text: qsTr("Save subtitles using the video name")
                        checkable: true
                        checked: root.app.saveWithVideoName
                        onToggled: {
                            // A Shift+click maps without switching (legacy
                            // MenuDialog::SendEvent toggles only without Shift).
                            if (root.hotkeyGesture("GLOBAL_SAVE_WITH_VIDEO_NAME"))
                                checked = Qt.binding(() => root.app.saveWithVideoName)
                            else
                                root.app.saveWithVideoName = checked
                        }
                    }
                }
                ShellMenuItem {
                    command: "GLOBAL_OPEN_AUTO_SAVE"
                    help: qsTr("Opens the selected autosave from the list") // HikariSubFrame.cpp:196
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
                    help: qsTr("Opens the temporary file removal window") // HikariSubFrame.cpp:197
                    iconRole: "remove-temporary"
                    objectName: "removeTemporaryMenuItem"
                    action: Action {
                        id: removeTemporaryAction
                        text: qsTr("Remove temporary files")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_DELETE_TEMPORARY_FILES")) autosaves.show(true)
                    }
                }
                // NT1: the Log panel, shown or hidden (legacy's place in File).
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
                // P6: legacy "Last session" submenu.
                ShellMenu {
                    help: qsTr("Last session options") // HikariSubFrame.cpp:200
                    iconRole: "last-session"
                    objectName: "lastSessionMenu"
                    title: qsTr("Last session")
                    ShellMenuItem {
                        command: "GLOBAL_LOAD_LAST_SESSION"
                        help: qsTr("Loads previously loaded files") // HikariSubFrame.cpp:161
                        id: loadLastSessionItem
                        iconRole: "last-session"
                        objectName: "loadLastSessionMenuItem"
                        text: qsTr("Load last session")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_LAST_SESSION")) sessionWindows.load()
                    }
                    ShellMenuItem {
                        command: "GLOBAL_LOAD_EXTERNAL_SESSION"
                        help: qsTr("Loads session from saved session file") // HikariSubFrame.cpp:163
                        id: loadSessionFileItem
                        iconRole: "session-open"
                        objectName: "loadSessionFileMenuItem"
                        text: qsTr("Load session from file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_EXTERNAL_SESSION")) sessionWindows.chooseSessionToLoad()
                    }
                    ShellMenuItem {
                        command: "GLOBAL_SAVE_EXTERNAL_SESSION"
                        help: qsTr("Saves session to file") // HikariSubFrame.cpp:165
                        id: saveSessionFileItem
                        iconRole: "session-save"
                        objectName: "saveSessionFileMenuItem"
                        text: qsTr("Save session to file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_EXTERNAL_SESSION")) sessionWindows.chooseSessionToSave()
                    }
                    ShellMenuItem {
                        help: qsTr("Asks whether to load previously loaded files at program startup") // HikariSubFrame.cpp:168
                        objectName: "askForLastSessionMenuItem"
                        text: qsTr("Ask whether to load the last session at program startup")
                        checkable: true
                        checked: root.app.sessionRestore === 1
                        keepsMenuOpen: false // ITEM_CHECK_AND_HIDE (HikariSubFrame.cpp:171)
                        onToggled: root.app.sessionRestore = checked ? 1 : 0
                    }
                    ShellMenuItem {
                        help: qsTr("Loads previously loaded files at program startup") // HikariSubFrame.cpp:172
                        objectName: "loadLastSessionOnStartMenuItem"
                        text: qsTr("Load last session after program start")
                        checkable: true
                        checked: root.app.sessionRestore === 2
                        keepsMenuOpen: false // ITEM_CHECK_AND_HIDE (HikariSubFrame.cpp:175)
                        onToggled: root.app.sessionRestore = checked ? 2 : 0
                    }
                }
                // O1: legacy GLOBAL_SETTINGS, the Options dialog.
                ShellMenuItem {
                    command: "GLOBAL_SETTINGS"
                    help: qsTr("Program settings") // HikariSubFrame.cpp:202
                    iconRole: "settings"
                    objectName: "settingsMenuItem"
                    action: Action {
                        id: settingsAction
                        text: qsTr("&Settings")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SETTINGS")) root.openSettings()
                    }
                }
                // O3: the one-shot legacy settings importer (no legacy item).
                ShellMenuItem {
                    iconRole: "import"
                    objectName: "importSettingsMenuItem"
                    text: qsTr("Import legacy settings...")
                    enabled: root.settingsImport.available
                    onTriggered: settingsImportDialog.openDialog()
                }
                ShellMenuItem {
                    help: qsTr("Exit the program") // HikariSubFrame.cpp:204
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
                help: qsTr("Undo") // HikariSubFrame.cpp:209
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
                help: qsTr("Redo") // HikariSubFrame.cpp:214
                iconRole: "redo"
                action: Action {
                    id: redoAction
                    text: qsTr("&Redo")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_REDO")) root.editor.redo()
                }
            }
            // Legacy GLOBAL_SORT_LINES / GLOBAL_SORT_SELECTED_LINES submenus.
            ShellMenu {
                help: qsTr("Sorts all lines in ASS file") // HikariSubFrame.cpp:237
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
                help: qsTr("Sorts selected lines in ASS file") // HikariSubFrame.cpp:239
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
                help: qsTr("Undo to last save") // HikariSubFrame.cpp:211
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
                help: qsTr("History") // HikariSubFrame.cpp:216
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
                help: qsTr("Turns on multireplacer") // HikariSubFrame.cpp:241
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
                help: qsTr("Selects lines by expressions") // HikariSubFrame.cpp:243
                iconRole: "select-lines"
                objectName: "selectLinesMenuItem"
                action: Action {
                    id: selectLinesAction
                    text: qsTr("Select &lines")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SELECT_LINES")) selectLinesDialog.openDialog()
                }
            }
            // F1: legacy GLOBAL_FIND_REPLACE, GLOBAL_SEARCH and GLOBAL_FIND_NEXT.
            ShellMenuItem {
                command: "GLOBAL_FIND_REPLACE"
                help: qsTr("Searches for the specified text phrases and replaces them") // HikariSubFrame.cpp:218
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
                help: qsTr("Searches for the specified text phrase") // HikariSubFrame.cpp:220
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
                help: qsTr("Finds the next occurrence of the phrase in the text") // HikariSubFrame.cpp:222
                iconRole: "search"
                objectName: "findNextMenuItem"
                action: Action {
                    id: findNextAction
                    text: qsTr("Find next")
                    // A question box waits: nothing re-enters the search (legacy's are modal).
                    enabled: root.editor.hasLine && !root.app.findBusy
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_NEXT")) root.app.findNext()
                }
            }
        }
        ShellMenu {
            id: automationMenu
            objectName: "automationMenu"
            title: qsTr("&Automation")
            // S4: legacy BuildMenu on opening (the Document's scripts, changed files reloaded).
            onAboutToShow: root.automation.menuOpened()
            // AM1: legacy's "Open shortcut mapping window" is gone with its
            // window (AM1-hotkeys-window-retired); each macro's shortcut is
            // in the Automation panel and Settings → Hotkeys.
            ShellMenuItem {
                command: "GLOBAL_AUTOMATION_LOAD_SCRIPT"
                help: qsTr("Load script") // HikariSubFrame.cpp:349
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
                help: qsTr("Refresh autoload scripts") // HikariSubFrame.cpp:351
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
                    // Legacy's modal progress dialog blocks it while a macro
                    // runs; D2: OnMenuOpened disables m_AutoMenu's items. AM1:
                    // off under a modal dialog, as the macros' shortcuts.
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
            // AD2-repeat-last-run: the last run again with its answers,
            // without its dialogs when they are unchanged.
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
            // AD3: the progress card's Cancel from the menu (it has no
            // hotkey of its own).
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
            // The macros go after it, whatever static items come before.
            MenuSeparator {
                id: macrosSeparator
                objectName: "macrosSeparator"
            }
            Instantiator {
                model: root.macroItems
                delegate: ShellMenuItem {
                    required property var modelData
                    objectName: "macro_" + modelData.name
                    text: modelData.name
                    enabled: !root.automation.running
                    // Automation.cpp:1376-1379: Shift maps the macro's hotkey
                    // (OnMapHkey(-1, "Script <file>-<n>")); AM1: in the
                    // Automation panel, which records it in place
                    // (AM1-hotkeys-window-retired).
                    onTriggered: {
                        if (root.hotkeys.shiftClicked(true)) {
                            root.showPanel(automationDock)
                            automationTool.record(root.automationHotkeys.legacyNameFor(modelData.path, modelData.ordinal))
                        } else {
                            root.automationManager.run(modelData.path, modelData.ordinal)
                        }
                    }
                }
                onObjectAdded: (index, object) => {
                    let at = 0
                    while (at < automationMenu.count && automationMenu.itemAt(at) !== macrosSeparator)
                        ++at
                    automationMenu.insertItem(at + 1 + index, object)
                }
                onObjectRemoved: (index, object) => automationMenu.removeItem(object)
            }
        }
        ShellMenu {
            objectName: "videoMenu"
            title: qsTr("&Video")
            // Each item: Shift+click maps its Global hotkey (OnMenuSelected).
            ShellMenuItem {
                command: "GLOBAL_OPEN_VIDEO"
                help: qsTr("Opens video file") // HikariSubFrame.cpp:247
                iconRole: "open-video"
                action: Action {
                    text: qsTr("Open &video…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.show()
                }
            }
            // V3: legacy's order (HikariSubFrame.cpp:246-258): the recent
            // videos (GLOBAL_RECENT_VIDEO), Open keyframes, the recent
            // keyframes (GLOBAL_RECENT_KEYFRAMES) and Open dummy video.
            // Legacy OnMenuOpened (HikariSubFrame.cpp:2252-2275) enables
            // Open keyframes and the recent keyframes with a video loaded, and
            // OnMenuSelected checks that for their hotkeys too (:681-687).
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
                help: qsTr("Open keyframes") // HikariSubFrame.cpp:252
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
                enabled: root.video.hasVideo // D2: OnMenuOpened's VidMenu
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
            // V6: GLOBAL_SET_START_TIME / GLOBAL_SET_END_TIME (HikariSubFrame.cpp:259-262;
            // OnMenuOpened enables them with a video and the editor).
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
                help: qsTr("Go to previous frame") // HikariSubFrame.cpp:263
                iconRole: "frame-previous"
                action: Action {
                    id: previousFrameAction
                    text: qsTr("Previous frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PREVIOUS_FRAME")) root.video.stepFrames(-1)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_NEXT_FRAME"
                help: qsTr("Go to next frame") // HikariSubFrame.cpp:265
                iconRole: "frame-next"
                action: Action {
                    id: nextFrameAction
                    text: qsTr("Next frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_NEXT_FRAME")) root.video.stepFrames(1)
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_VIDEO_AT_START_TIME"
                help: qsTr("Moves video to start time") // HikariSubFrame.cpp:267
                iconRole: "video-to-start-time"
                action: Action {
                    id: goToStartAction
                    text: qsTr("Go to start time"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_START_TIME")) root.video.goToLineStart()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_SET_VIDEO_AT_END_TIME"
                help: qsTr("Moves video to end time") // HikariSubFrame.cpp:270
                iconRole: "video-to-end-time"
                action: Action {
                    id: goToEndAction
                    text: qsTr("Go to end time of line"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_END_TIME")) root.video.goToLineEnd()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_PLAY_PAUSE"
                help: qsTr("Plays / Pauses video") // HikariSubFrame.cpp:273
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
            // A3: GLOBAL_SET_AUDIO_FROM_VIDEO, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO
            // (legacy OnMenuOpened: ABox != nullptr && editor; the editor is
            // the editing target's).
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
            // V3: legacy's video context menu entries (VideoBox.cpp:979-1016),
            // which the context menu has in legacy's form, in the Video menu
            // too (V3-video-menu-entries): Unload video (VIDEO_DELETE_FILE,
            // V3-unload-video), the streams and the chapters.
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
            // V4: GLOBAL_VIDEO_ZOOM and GLOBAL_RESET_VIDEO_ZOOM (legacy
            // OnMenuOpened: a loaded video; the reset also a zoom != 1).
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
            // KE1-render-cost-overlay: what the frame's subtitles cost to draw.
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
        // A1: legacy Audio menu (GLOBAL_OPEN_AUDIO, GLOBAL_RECENT_AUDIO,
        // GLOBAL_AUDIO_FROM_VIDEO, GLOBAL_CLOSE_AUDIO). AV3 (#240) retired
        // GLOBAL_OPEN_DUMMY_AUDIO: a dummy video brings its own silent audio.
        ShellMenu {
            id: audioMenu
            objectName: "audioMenu"
            title: qsTr("A&udio")
            ShellMenuItem {
                command: "GLOBAL_OPEN_AUDIO"
                help: qsTr("Opens audio file") // HikariSubFrame.cpp:293
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
                help: qsTr("Recently opened audio") // HikariSubFrame.cpp:297
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
                        help: qsTr("Open") + " " + modelData.path // HikariSubFrame.cpp:1574
                        text: modelData.label
                        // P9: Ctrl+click shows the file in its folder.
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
                help: qsTr("Opens audio from video") // HikariSubFrame.cpp:299
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
                help: qsTr("Closes audio") // HikariSubFrame.cpp:303
                iconRole: "close-audio"
                objectName: "closeAudioMenuItem"
                action: Action {
                    id: closeAudioAction
                    text: qsTr("Close audio")
                    enabled: root.audio.hasAudio
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_AUDIO")) root.audio.closeAudio()
                }
            }
        }
        ShellMenu {
            id: viewMenu
            objectName: "viewMenu"
            // Legacy ViewMenu "View" (HikariSubFrame.cpp:307-313); &w, as
            // Alt+V stays with &Video.
            title: qsTr("Vie&w")
            // WS3 (#217): legacy's five views (GLOBAL_VIEW_*) are retired;
            // the current tab's layout is chosen here and in the layout
            // switcher, the same menu: the presets, the saved layouts, Save
            // layout as…, Reset this layout and Manage layouts….
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
            // D1: each panel can be shown (and focused), hidden, floated or
            // docked.
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
        }
        // Legacy Subtitles menu; its entries join as their cards land.
        ShellMenu {
            objectName: "subtitlesMenu"
            title: qsTr("&Subtitles")
            ShellMenuItem {
                command: "GLOBAL_SHOW_SHIFT_TIMES"
                help: qsTr("Shifting subtitle times") // HikariSubFrame.cpp:336
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
                text: qsTr("Shift times / run time post processor")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_TIMES")) root.runShiftTimes()
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_STYLE_MANAGER"
                help: qsTr("Is used to manage ASS styles") // HikariSubFrame.cpp:320
                id: styleManagerItem
                iconRole: "styles"
                objectName: "styleManagerMenuItem"
                text: qsTr("Style &manager")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_STYLE_MANAGER")) styleManagerWindow.showFor(root.app.activeLineStyle())
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_ASS_PROPERTIES"
                help: qsTr("ASS subtitle properties") // HikariSubFrame.cpp:318
                id: assPropertiesItem
                iconRole: "script-properties"
                objectName: "assProperties"
                text: qsTr("ASS file properties")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_ASS_PROPERTIES")) root.openScriptProperties()
            }
            ShellMenu {
                help: qsTr("Converts from one format to another") // HikariSubFrame.cpp:334
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
            // Legacy SubsMenu: Font collector after Conversion and Shift times
            // (HikariSubFrame.cpp:338), enabled for subsFormat < SRT (2407).
            ShellMenuItem {
                command: "GLOBAL_OPEN_FONT_COLLECTOR"
                help: qsTr("Font collector") // HikariSubFrame.cpp:338
                id: fontCollectorItem
                objectName: "fontCollectorMenuItem"
                iconRole: "font-collector"
                text: qsTr("Font collector")
                enabled: root.shell.hasEditingTarget && root.shell.assColumns
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_FONT_COLLECTOR")) root.openFontCollector()
            }
            ShellMenuItem {
                command: "GLOBAL_OPEN_SUBS_RESAMPLE"
                help: qsTr("Resample subtitles") // HikariSubFrame.cpp:340
                id: resampleItem
                iconRole: "resample"
                objectName: "resampleMenuItem"
                text: qsTr("Resample subtitles")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS_RESAMPLE")) root.openResample()
            }
            // Legacy HikariSubFrame: after Resample subtitles.
            ShellMenuItem {
                command: "GLOBAL_OPEN_SPELLCHECKER"
                help: qsTr("Check spelling") // HikariSubFrame.cpp:342
                id: checkSpellingItem
                iconRole: "spellchecker"
                objectName: "checkSpellingMenuItem"
                text: qsTr("Check spelling")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SPELLCHECKER")) root.openSpellChecker()
            }
            // KA7: the Karaoke check window, after Check spelling.
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
            // KA3: the Kanji timer window, after Check karaoke.
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
            // TM1 (#232): the editing target's Translation mode, which the
            // Line editor's "Translator mode" check box switched in legacy
            // (TM1-translator-check-removed). On is one "Turning on translator
            // mode" step; off asks first (TranslationModeOffConfirm).
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
            MenuSeparator {}
            // E6: legacy SubsMenu's last item, "Hides tags in ASS and MDVD"
            // (HikariSubFrame.cpp:344-345): the Grid's switch.
            ShellMenuItem {
                command: "GLOBAL_HIDE_TAGS"
                id: hideTagsItem
                iconRole: "hide-tags"
                objectName: "hideTagsMenuItem"
                text: qsTr("Hide tags")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_HIDE_TAGS")) root.app.toggleHideTags()
            }
            // LE3-tags-shown: the Line editor's hidden-tag view, a setting
            // (editor.hideTags) and this command; the Grid's switch above
            // stays its own.
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
            // FR1 (#363): the setup of the first start, any time.
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
                // DX1 (#367): legacy's "HikariSub website" (HikariSubFrame.cpp:360)
                // is the user guide now, at the page of the panel or window in use.
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
                help: qsTr("Opens the HikariSub issue tracker") // HikariSubFrame.cpp:362
                iconRole: "report-issue"
                objectName: "reportIssueMenuItem"
                action: Action {
                    id: reportIssueAction
                    text: qsTr("&Report an issue")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ANSI")) root.app.reportIssue()
                }
            }
            ShellMenuItem {
                command: "GLOBAL_CHECK_FOR_UPDATES"
                help: qsTr("Checks whether a newer version is available") // HikariSubFrame.cpp:364
                iconRole: "check-updates"
                objectName: "checkForUpdatesMenuItem"
                action: Action {
                    id: checkForUpdatesAction
                    text: qsTr("Check for &updates")
                    enabled: !root.updates.checking
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_CHECK_FOR_UPDATES")) root.updates.checkNow()
                }
            }
            // UD1 (#362): the user folder in the file manager; no hotkey.
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
            // SH3 (#223): the release notes since the version used before; no hotkey.
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
                help: qsTr("Shows program info") // HikariSubFrame.cpp:366
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
                help: qsTr("Shows credits") // HikariSubFrame.cpp:368
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

    // One text role of the Line editor (Original or Translated). The field
    // mirrors the editor's projection; user changes go to the controller,
    // which maps them onto the raw source. Programmatic updates are not edits.
    component RoleField: TextArea {
        id: field
        required property int role
        readonly property string shown: role === 0 ? root.editor.text : root.editor.translationText
        // TM1: the field Tab or Shift+Tab reaches across panels, when set.
        property Item tabNext: null
        property Item tabPrevious: null
        readOnly: !root.editor.editable
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true // the Original's selection survives for "Paste the selected"
        Layout.fillWidth: true
        Layout.fillHeight: true
        // E4: the Line's fields take rows below; a short panel keeps a line of text.
        Layout.minimumHeight: topPadding + bottomPadding + cursorRectangle.height

        property bool syncing: false
        // Where `inner` starts in `outer` past its carried tags: a match
        // after whole override blocks and the spaces among them ("\h" before
        // a leading tag), not one inside a tag ("an8" in "{\an8}an8").
        // Where `inner` starts in `outer` past a prefix of override blocks
        // and ASS spaces ending with a block, or -1. Linear in both: the
        // prefix walked once for where its blocks end, `inner` found by
        // KMP (a long Line's prefix is never scanned per candidate). The
        // block ends kept by position: V4's Set.has is linear. Past what
        // Tags from original carries (an Original and a translation of 3 *
        // 16384 units each) no tags were carried: nothing is walked.
        function carriedPrefix(outer, inner) {
            if (outer.length > 6 * 16384 || inner.length > 6 * 16384)
                return -1
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
            // TM1: the carried tags shown or left out around the same text
            // (Tags from original switched): the caret keeps its place in it.
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
            // E5: an untranslated Line with tags gives the Translated field the
            // focus (TM1's Tags from original; legacy's Moving tags), in its
            // window (the Original text panel may float in another).
            function onFieldFocusRequested(role) {
                if (role === field.role)
                    root.focusPanel(field, Qt.OtherFocusReason)
            }
        }
        // The rows of text from the rectangle of one position to another's,
        // each {x, y, width} with y at its foot: an underline on each.
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
        // TM1: the Original's runs (editor.tagRuns): a numbered badge where
        // each starts in the Original text, and in the Translated text a
        // badge and an underline under the text it is placed on, or a dotted
        // underline under the same text found unplaced (a suggestion: Tab
        // places it, Escape dismisses it).
        Repeater {
            model: root.editor.translationMode ? root.editor.tagRuns : []
            delegate: Item {
                id: run
                required property var modelData
                anchors.fill: parent
                z: 2
                // Every suggestion Tab would place is shown, a wrapped one
                // on every row as a placed run's underline is.
                Item {
                    id: suggestion
                    readonly property int from: field.role === 1 ? run.modelData.suggestedStart : -1
                    readonly property int to: field.role === 1 ? run.modelData.suggestedEnd : -1
                    readonly property rect first: { field.text; field.width; return field.positionToRectangle(from) }
                    readonly property rect last: { field.text; field.width; return field.positionToRectangle(to) }
                    objectName: field.objectName + "Run" + run.modelData.number + "Suggestion"
                    visible: from >= 0 && to > from && to <= field.length
                    anchors.fill: parent
                    // The dotted underline, 2 px dots 2 px apart on every
                    // row, in one Shape: no item per dot or row.
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
                    // The underline, on every row a wrapped run takes.
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
        // F3: the spell-checked field (legacy TextEdit: the Translated one in
        // translation mode) marks misspellings and bracket errors while
        // spelling is on, and its menu offers suggestions, the Spellchecker
        // switch, the installed languages and "Add word".
        readonly property bool spelled: role === (root.editor.translationMode ? 1 : 0)
        property var misspell: ({})
        property int misspellPosition: -1
        function refreshMarks() {
            spellMarks.ranges = spelled ? root.app.editorSpellingMarks(role) : []
        }
        onSpelledChanged: refreshMarks()
        // E4: the text in legacy TextEditor's colours (tags, line breaks and
        // drawings; a template or code comment's code), and F3's marks.
        EditorHighlighter {
            id: spellMarks
            objectName: field.objectName + "Highlighter"
            document: field.textDocument
            // EDITOR_SPELLCHECKER: the theme's (K2; legacy's dark and light defaults).
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
        }
        ContextMenu.onRequested: position => {
            field.misspellPosition = field.positionAt(position.x, position.y)
            field.misspell = field.spelled ? root.app.editorMisspellAt(field.role, field.misspellPosition) : ({})
        }
        ContextMenu.menu: ShellMenu {
            id: editMenu
            objectName: field.objectName + "Menu"
            readonly property var suggestions: field.misspell.suggestions || []
            Instantiator {
                model: editMenu.suggestions
                delegate: ShellMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: root.app.replaceEditorMisspell(field.role, field.misspellPosition, modelData)
                }
                onObjectAdded: (index, object) => editMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => editMenu.removeItem(object)
            }
            MenuSeparator { visible: editMenu.suggestions.length > 0; height: visible ? implicitHeight : 0 }
            ShellMenuItem { iconRole: "edit-copy"; text: qsTranslate("HikariSub.Editor", "&Copy"); enabled: field.selectedText.length > 0; onTriggered: field.copy() }
            ShellMenuItem { iconRole: "edit-cut"; text: qsTranslate("HikariSub.Editor", "Cu&t"); enabled: field.selectedText.length > 0 && !field.readOnly; onTriggered: field.cut() }
            ShellMenuItem { iconRole: "edit-paste"; text: qsTranslate("HikariSub.Editor", "&Paste"); enabled: !field.readOnly; onTriggered: field.paste() }
            MenuSeparator {}
            ShellMenuItem {
                id: spellingOnItem
                iconRole: "spellchecker"
                objectName: field.objectName + "SpellingOn"
                text: qsTranslate("HikariSub.Editor", "Spellchecker")
                checkable: true
                checked: root.app.spellingOn
                keepsMenuOpen: false // ITEM_CHECK_AND_HIDE (DialogueTextEditor.cpp:2326)
                visible: field.spelled
                height: visible ? implicitHeight : 0
                onTriggered: root.app.spellingOn = checked
            }
            // "Installed languages": only in the spell-checked field (legacy
            // builds it with the Spellchecker entry, useSpellchecker).
            Instantiator {
                model: field.spelled ? 1 : 0
                delegate: ShellMenu {
                    id: languagesMenu
                    iconRole: "dictionary"
                    objectName: field.objectName + "Languages"
                    title: qsTranslate("HikariSub.Editor", "Installed languages")
                    property var languages: []
                    onAboutToShow: languages = root.app.dictionaries()
                    Instantiator {
                        model: languagesMenu.languages
                        delegate: ShellMenuItem {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            // Legacy marks the entry whose name is the chosen language's.
                            checked: modelData.name === root.app.dictionaryName(root.app.dictionaryLanguage)
                            keepsMenuOpen: false // ITEM_RADIO
                            onTriggered: root.app.dictionaryLanguage = modelData.symbol
                        }
                        onObjectAdded: (index, object) => languagesMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => languagesMenu.removeItem(object)
                    }
                }
                onObjectAdded: (index, object) => {
                    // Right after the Spellchecker switch.
                    for (let i = 0; i < editMenu.count; ++i) {
                        if (editMenu.itemAt(i) === spellingOnItem) {
                            editMenu.insertMenu(i + 1, object)
                            return
                        }
                    }
                    editMenu.addMenu(object)
                }
                onObjectRemoved: (index, object) => editMenu.removeMenu(object)
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
        // EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK: a double click on a
        // misspelling lists its suggestions ("Fix suggestions").
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: eventPoint => {
                if (!root.app.suggestionsOnDoubleClick || !field.spelled)
                    return
                const position = field.positionAt(eventPoint.position.x, eventPoint.position.y)
                const found = root.app.editorMisspellAt(field.role, position)
                if (found.word) {
                    // Legacy shows the list and returns before its double
                    // click selects the word: the caret stays where clicked.
                    field.deselect()
                    field.cursorPosition = position
                    fixSuggestions.openFor(field, field.role, position, found.suggestions)
                }
            }
        }
        // E6-ime-enter-held (decision 60): an Enter that comes with an input
        // method's end of composition only ends it. Windows' Microsoft Korean
        // IME commits the syllable and then passes the same Enter on as a key
        // (E6 Windows evidence K7); ibus-hangul clears the preedit, commits
        // and forwards it. So an Enter while composing finishes the
        // composition, and the first key after a composition ends, if it is
        // an Enter within compositionKeyWindow ms, is taken and does nothing:
        // no commit, no advance, no tag put. Any other key, the release of an
        // Enter (the Japanese IME keeps the Enter that confirms, J4, and only
        // its release reaches the field), a click or a focus change ends the
        // wait, so a second, separate Enter commits as usual. CodeMirror 6
        // and ProseMirror ignore one Enter within 100 or 500 ms of
        // compositionend the same way.
        // E6-ime-esc-list-first (decision 60): Escape the same way cancels
        // the composition and closes the tag list, keeping the draft (K8;
        // the Japanese IME keeps its Escape, J5); a second, separate Escape
        // reverts the draft as usual (K10).
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
        // Legacy EDITBOX bindings (O2, HotkeysController::actionFor): the
        // text field's own plain Enter (commit and go to the next line) and
        // TabPanel's fixed numpad Enter and Ctrl+numpad Enter first, then the
        // Editor's bindings, which act before the application's shortcuts;
        // composition keeps its own Enter (compositionKey above). Escape
        // discards the draft and Ctrl+Z / Ctrl+Y undo and redo it (accepted
        // transaction policy).
        function hotkeyAction(event) {
            const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            if (enter && field.inputMethodComposing)
                return ""
            if (enter && mods === 0)
                return "EDITBOX_COMMIT_GO_NEXT_LINE"
            if (event.key === Qt.Key_Enter && mods === Qt.ControlModifier)
                return "EDITBOX_COMMIT"
            return root.hotkeys.actionFor(2, event.key, event.modifiers)
        }
        // TextEditor::OnKeyPress (DialogueTextEditor.cpp:534-545): Tab never
        // goes into the text; it is a navigation event, forward or with
        // Shift backward, to the next control in the tab order (the tag
        // list, which takes its own keys first, closes as the field loses
        // focus). Its window change flag (Ctrl) reached EditBox's
        // HikariContainer::OnNavigation first (HikariPanel.cpp:23), which
        // ignores it, so Ctrl+Tab and Ctrl+Shift+Tab move the same way.
        // TM1: from the Original text panel, Tab goes on to the Translated
        // text, and Shift+Tab comes back, as in legacy's one EditBox. The
        // other panel may float in its own window, which focusPanel activates,
        // or be the hidden tab of the same group, which comes to the front.
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
        // Undo and Redo go through the draft first (editor-shortcuts.md):
        // the field takes Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z whatever the
        // platform's own keys, or xcb's Ctrl+Y would reach GLOBAL_REDO (KB3).
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
            field.compositionEndedAt = -1 // only the first key can be the composition's
            if (tagListPopup.key(event)) {
                event.accepted = true
                return
            }
            const ctrl = event.modifiers & Qt.ControlModifier
            const action = hotkeyAction(event)
            // TM1: a plain Tab places the suggested runs and Escape dismisses
            // them; without suggestions they move on and discard as usual.
            const plain = (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
            if (action !== "") {
                root.runEditorHotkey(action, field) // an action not here yet still takes the key
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
        // E6: the tag list (TextEditor's PopupTagList), on the raw text only:
        // the hidden-tag view refuses ASS syntax, so no tag is typed there.
        EditorTagList {
            id: tagListPopup
            field: field
            controller: root.editor.tagList
            enabled: root.editor.showTags && !field.readOnly
        }
    }

    // A panel's body. Its dock's header (DockTabBar.qml: a title bar or its
    // tab) is its only visible header: the user dropped the in-panel title
    // row on 2026-10-05, since with docking it repeated the dock's title. The
    // title stays the panel's name for assistive technology.
    component Panel: FocusScope {
        id: panel
        property string title
        // The panel's name for assistive technology: its title unless the
        // title names something else (the Grid's names the editing target).
        property string accessibleName: title
        default property alias content: body.data
        readonly property real bodyHeight: body.height
        // D3: the dock's uniqueName, and the smallest body its content takes
        // unclipped. The docking engine keeps the panel at least that big
        // (with the body's margins), so dragging a separator cannot squeeze
        // it into clipping its controls.
        property string dockName
        property size minimumBodySize: Qt.size(0, 0)
        // The item holding the panel's key handlers (KeyRouter), when not
        // the panel itself.
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
        // A panel squeezed below its content's size cuts it off rather than
        // draw over the next panel (the body's margin holds a control's
        // 1-wide focus ring).
        clip: true
        Accessible.role: Accessible.Pane
        Accessible.name: accessibleName
        Accessible.description: accessibleName !== title ? title : ""

        // K2 (visual-language.md, "Keyboard focus"): the panel holding the
        // focus shows the accent corner mark on its dock header
        // (DockTabBar.qml, PanelFocusMark.qml). D3: no frame of its own; the
        // docking engine's 1-pixel separators are the boundaries between
        // panels (MuseScore's).
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

    // Dropped files open by the legacy rules (subtitles, scripts, video).
    // It takes file drags only and stays out of the docking engine's way
    // (D1): declared before the docking area, so the engine's own hit test
    // (the last child under the cursor wins; X11, Windows) finds the panels,
    // and above it (z) for real drag-and-drop, where it refuses every drag
    // without files so the engine's drop indicators get it (Wayland).
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
            // P9: legacy OpenFiles: one file opens as OpenFile does; several
            // open as tabs, after a review of the editing target's work.
            const result = root.app.openDropped(drop.urls)
            if (result.kind === "subtitles")
                root.openSubtitles(result.path)
            else if (result.kind === "video")
                root.openSingleVideo(result.path)
            else if (result.kind === "files" && result.rows.length > 0)
                closeReview.review(result.rows)
        }
    }

    // Wayland: the docking engine drags a panel with real drag-and-drop, and
    // its QtQuick frontend takes such drops only in floating windows (a QML
    // DropArea naming the engine's drop area in dropAreaCpp). This gives the
    // main window the same receiver, under the file drop target. Elsewhere
    // the engine finds drop areas by hit test, so it is hidden there.
    DropArea {
        id: panelDropArea
        objectName: "panelDropArea"
        anchors.fill: dockingArea
        z: 1
        visible: Qt.platform.pluginName.startsWith("wayland")
        property QtObject dropAreaCpp: null
        // The engine makes the main window's drop area with the docking area.
        Component.onCompleted: Qt.callLater(() => dropAreaCpp = Docking.mainDropArea(dockingArea.uniqueName))
    }

    // SH2: the Home page over the docked panels while no Document is open,
    // under the file drop area (z), so files dropped on it open. The panels
    // under it stay laid out (their layout is saved as it is) but take no
    // input.
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
        // SH3: What's new, while there are notes to show.
        whatsNew: root.whatsNew.releases.length > 0 ? whatsNewCard : null
        // P9: Ctrl+click shows the file in its folder, as in File > Recent.
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
    // The first Document opened from Home: the focus, left nowhere when the
    // page went, goes to the Grid (else the first shown panel), unless the
    // opening put it somewhere.
    function focusAfterHome() {
        if (homePage.visible || (root.activeFocusItem && root.activeFocusItem !== root.contentItem))
            return
        const shown = root.panels.filter(p => p.visible)
        const next = shown.includes(gridPanel) ? gridPanel : shown[0]
        if (next)
            root.focusPanel(next, Qt.OtherFocusReason)
    }

    // D1: the Classic panels dock, float, tab and close (KDDockWidgets behind
    // ui/docking.h; docs/qt/docking.md). Closing a panel hides its view; its
    // controller and any draft stay.
    KDDW.DockingArea {
        id: dockingArea
        objectName: "dockingArea"
        anchors.fill: parent
        uniqueName: "Classic"
        enabled: !homePage.visible // SH2

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
                // The controls below the video at their narrowest, above
                // two rail buttons' height. The
                // inspector bar counts with its tool's own lines; what it
                // holds for a taller tool comes out of the height above that
                // (VT1), so the minimum does not grow with it.
                minimumBodySize: Qt.size(videoTransport.implicitWidth, controlsOwnHeight + 72)
                // Measured, not bound (the held height follows the room this
                // leaves), whenever the column's height or the held height
                // changes: a switch to a tool with more lines can leave the
                // column as tall, holding less (Codex on #293). The bar's
                // layout and the column are laid out first, so the two
                // numbers are of one layout.
                property real controlsOwnHeight: 0
                function measureControls() {
                    videoToolBar.contentItem.ensurePolished()
                    videoControls.ensurePolished()
                    controlsOwnHeight = videoControls.implicitHeight - videoToolBar.held
                }
                Connections {
                    target: videoControls
                    function onImplicitHeightChanged() { Qt.callLater(videoPanel.measureControls) }
                }
                Connections {
                    target: videoToolBar
                    function onHeldChanged() { Qt.callLater(videoPanel.measureControls) }
                }
                Component.onCompleted: measureControls()
                title: qsTranslate("HikariSub.Video", "Video")
                // Frame stepping is the Global bindings GLOBAL_PREVIOUS_FRAME /
                // GLOBAL_NEXT_FRAME (Left / Right by default), from the shell's
                // table (legacy VideoBox has no arrow keys of its own).
                // O2: the Video window's bindings (VideoBox's table; by default
                // VIDEO_PLAY_PAUSE Space, VIDEO_5_SECONDS_* L / ;,
                // VIDEO_MINUTE_* Up / Down), before the application's shortcuts.
                // T1: Esc cancels an open visual gesture first; keys no binding
                // takes go to the visual tool, as VideoBox::OnKeyPress hands
                // them to the Visuals. VT2-arrow-nudge: a tool's nudge (the
                // arrows, Shift a tenth) comes before the bindings, so they
                // reach it over the video, focused or hovered (KB1's router
                // asks this override).
                // FS1: VIDEO_FULLSCREEN_BAR is the fullscreen window's; here
                // its key (Tab) keeps moving the keyboard focus.
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
                    } else if (event.key === Qt.Key_F) { // V5: VideoBox::OnKeyPress's SetFullscreen()
                        root.videoFullscreen.toggle(0)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Menu) { // V4: WXK_WINDOWS_MENU, the menu at the pointer
                        videoContextMenu.openAt(root.videoView.cursorIn(visualOverlay))
                        event.accepted = true
                    } else if (root.videoView.key(event.key, event.modifiers)) { // V4: Return in the zoom mode, Ctrl+Shift+Z
                        event.accepted = true
                    } else if (root.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                        event.accepted = true
                    }
                }
                Keys.onReleased: event => event.accepted = root.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

                // The legacy "Associated files" confirmation, inline: the
                // Document stays editable whatever is chosen (P9: its text,
                // buttons and "Apply to All"). Over the picture, beside the
                // tool rail, so the rail stays whole and usable.
                AssociationOffer {
                    video: root.video
                    anchors { left: videoStageDock.left; right: parent.right; top: parent.top }
                    z: 1
                }
                // V3: the indexing's progress and Cancel, placed as the
                // question above.
                VideoIndexingProgress {
                    video: root.video
                    anchors { left: videoStageDock.left; right: parent.right; top: parent.top }
                    z: 1
                }
                // T1: the tool rail beside the canvas (layout A).
                VisualToolRail {
                    id: visualRail
                    tools: root.visualTools
                    keyOf: symbol => root.boundKeys(symbol, 3)
                    anchors { left: parent.left; top: parent.top; bottom: videoControls.top }
                }
                // Where the picture sits in the panel.
                Item {
                    id: videoStageDock
                    anchors { left: visualRail.visible ? visualRail.right : parent.left; right: parent.right
                              top: parent.top; bottom: videoControls.top }
                }
                // V5: the picture, the visual tools' overlay and the zoom
                // frame; they move into the fullscreen window while it is
                // shown (one presenter: the video is shown in one place).
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
                    // The visual tools' shared view places the frame (legacy UpdateRects).
                    videoRect: root.visualTools.videoRect
                    sourceRect: root.visualTools.sourceRect
                    Component.onCompleted: root.video.attachPresenter(presenter)
                }
                VisualOverlay {
                    id: visualOverlay
                    anchors.fill: presenter
                    tools: root.visualTools
                    focusTarget: root.videoFullscreen.active ? videoFullscreenWindow.keyTarget : videoPanel
                    // In fullscreen the pinned panel (m_PanelOnFullscreen) or none.
                    panelHeight: root.videoFullscreen.active ? videoFullscreenWindow.pinnedPanelHeight : videoControls.height
                    view: root.videoView // V4
                    // FS1-cursor-hide: the pointer hides over the fullscreen
                    // picture while nothing holds the bar.
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
                // V4: the zoom mode's frame, the context menu and the aspect ratio.
                VideoZoomFrame {
                    anchors.fill: presenter
                    view: root.videoView
                }
                // KE2: the Template editor's preview on the frame.
                VideoTemplateBadge {
                    id: templateBadge
                    x: presenter.x + 8
                    y: presenter.y + 8
                    visible: root.templateEditor.videoPreview && root.video.hasVideo
                    controller: root.templateEditor
                }
                // KE1-render-cost-overlay: what the frame's subtitles cost to
                // draw; below the preview badge when it shows.
                VideoRenderCost {
                    anchors.fill: presenter
                    visible: root.videoView.renderCost && root.video.hasVideo
                    cost: presenter.renderCost
                    textTop: templateBadge.visible ? templateBadge.height + 12 : 8
                }
                // HY1: Hydra's last change card, in the video's top-right corner.
                VisualLastChange {
                    anchors { top: parent.top; right: parent.right; margins: 14 }
                    tools: root.visualTools
                    visible: hasChange && root.video.hasVideo && root.visualTools.activeFamily === 10 // Family::Hydra
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
                // The seek bar and the legacy times field (keyframes highlighted).
                ColumnLayout {
                    id: videoControls
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    spacing: 2
                    // V4: the wheel over the panel is the volume's (VideoBox.cpp:519-534).
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => root.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
                    }
                VisualToolBar { // VT1: the tool, its options and values, the batch picker
                    id: videoToolBar
                    Layout.fillWidth: true
                    dialogs: !videoFullscreenWindow.visible // fullscreen's bar has them then
                    settings: root.app.settings
                    room: Math.max(0, videoPanel.bodyHeight - videoPanel.minimumBodySize.height)
                    // It wraps rather than widen the column: wider text (a
                    // translation, a larger font) or a narrow panel must not
                    // push Next frame out of the panel.
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
                    // It follows the reported frame (the player's while
                    // playing, at the texts' pace) except while held, as
                    // legacy VideoSlider's `block` kept its thumb under the
                    // mouse; only its own moves seek.
                    Binding on value {
                        when: !videoSeekBar.pressed
                        value: Math.max(0, root.video.frame)
                        restoreMode: Binding.RestoreNone
                    }
                    enabled: root.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTranslate("HikariSub.Video", "Video position")
                    // A drag seeks at most every 50 ms and once more where it
                    // is let go (V2 user decision 2026-10-14).
                    onMoved: root.video.seekBarMoved(Math.round(value), pressed)
                    onPressedChanged: if (!pressed) root.video.seekBarReleased()
                }
                RowLayout {
                    Layout.fillWidth: true
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
                        // Legacy's times field: its parts named in the tooltip.
                        Accessible.description: qsTranslate("HikariSub.Video", "Frame time; frame number; frames from the line's start frame; "
                                                     + "milliseconds from the line's start and end")
                        HoverHandler { id: timesHover }
                        ToolTip.visible: timesHover.hovered && text.length > 0
                        ToolTip.text: Accessible.description
                    }
                }
                RowLayout {
                    id: videoTransport
                    Layout.fillWidth: true
                    // Legacy VideoBox's bitmap buttons (VIDEO_PLAY_PAUSE,
                    // GLOBAL_PLAY_ACTUAL_LINE, VIDEO_STOP): the binding in the
                    // tooltip, and Shift+click maps it (BitmapButton). K1: the
                    // set's icons in place of legacy's bitmaps (play / pause
                    // as legacy ChangeButtonBMP swaps them, VideoBox.cpp:1414);
                    // the text stays the accessible name.
                    // V3: legacy's Previous file / Next file buttons around
                    // the transport (VideoBox.cpp:156-165), asking first.
                    // Flat tool buttons, as the audio box's (one button
                    // language for the two transports).
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
                    // A4: GLOBAL_PLAY_ACTUAL_LINE; legacy then focuses the
                    // Line editor's text.
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
                            root.focusPanel(lineText, Qt.OtherFocusReason) // TM1: maybe in a floating panel
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
                        focusPolicy: Qt.NoFocus // KB2: a click left Space to the button
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
                        focusPolicy: Qt.NoFocus // KB2: a click left Space to the button
                        onClicked: root.video.stepFrames(1)
                    }
                    VideoVolumeSlider { // V4: legacy VolSlider at the right
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
                // With audio open: the button row under a short display,
                // as narrow as the folded row (AU1).
                minimumBodySize: audioButtons.visible
                                 ? Qt.size(audioButtons.foldedWidth, 64 + audioScroll.implicitHeight + 4 + audioButtons.implicitHeight)
                                 : Qt.size(audioStatus.implicitWidth, audioStatus.implicitHeight)
                title: qsTranslate("HikariSub.Audio", "Audio")
                // The audio box in legacy AudioBox's layout: the display with
                // the search bar below it (DisplaySizer) and the button row
                // across the bottom (ButtonSizer). AU1 (#267): the sliders
                // beside them (TopSizer) moved into the zoom and speaker
                // popovers, so the display takes the full width.
                // KA2 (#277, KA2-tap-to-time): the tap bar above the display
                // while tap mode is on.
                AudioTapBar {
                    id: audioTapBar
                    audio: root.audio
                    visible: root.audio.loaded && root.audio.tapOn
                    height: visible ? implicitHeight : 0
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                }
                // A1: the waveform with its time ruler and marks (legacy AudioDisplay).
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
                // KA2-tap-to-time: the tapped syllables in a lane near the top
                // of the waveform, under the karaoke letters' row, the held
                // one growing at the playhead; a release before the next
                // press leaves a hatched gap.
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
                            // the gap before this syllable, hatched
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
                                // high contrast: outlined on the field, the held one filled
                                color: Theme.highContrast && !span.modelData.held ? Theme.field
                                     : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, span.modelData.held || Theme.highContrast ? 0.9 : 0.5)
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
                                textFormat: Text.PlainText // a Line's text, never markup (no <img> fetched)
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
                    // legacy DisplaySizer: wxBOTTOM 4 under the search bar
                    anchors { left: parent.left; right: audioSliderColumn.visible ? audioSliderColumn.left : parent.right
                              bottom: audioButtons.top; bottomMargin: visible ? 4 : 0 }
                    height: visible ? implicitHeight : 0
                    size: root.audio.scrollRange > 0 ? Math.min(1, root.audio.scrollPage / root.audio.scrollRange) : 1
                    // A visible track in the boundary colour: the handle alone
                    // read as a stray pill.
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
                        if (pressed) // legacy AudioBox::OnScrollbar
                            root.audio.setScrollPosition(Math.round(position * root.audio.scrollRange))
                    }
                    // #385: no tooltip; legacy's "Search bar" named it wrongly.
                }
                // AU2 (#268): legacy's slider column beside the display and
                // its search bar (TopSizer, AudioBox.cpp:105-143), shown from
                // Customise: the horizontal stretch, then the vertical
                // stretch and the volume with Link under them. The zoom and
                // speaker popovers keep the same values.
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
                    // legacy HorizontalZoom: 0 at the top (wxSL_VERTICAL)
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
                            // legacy VerticalZoom and VolumeBar: the top end
                            // at the top (wxSL_INVERSE)
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
                        // legacy VerticalLink, under the two it links
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
                // Legacy MappedButton's tooltip for an AUDIO_HOTKEY action:
                // the bindings ("A or B" for the buttons with two,
                // SetTwoHotkeys: the id and the id - 10) and the gesture.
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
                // AU1 (#267): Play around's plays, in its menu's order, with
                // legacy's button tips (AudioBox.cpp:149-193). audio.playAround
                // holds the one the face shows and a click repeats: the last
                // one chosen from the menu (a hotkey plays its range without
                // changing it). AU2: `id` is the play's in
                // audio.panelControls, `label` its menu text.
                readonly property var playArounds: [
                    { id: "before", symbol: "AUDIO_PLAY_500MS_BEFORE", icon: "play-before-start",
                      tip: qsTranslate("HikariSub.Audio", "Play 500ms before the start time"),
                      label: qsTranslate("HikariSub.Audio", "Play 500 ms before the start") },
                    { id: "first", symbol: "AUDIO_PLAY_500MS_FIRST", icon: "play-after-start",
                      tip: qsTranslate("HikariSub.Audio", "Play 500 ms after the start time"),
                      label: qsTranslate("HikariSub.Audio", "Play the first 500 ms") },
                    { id: "last", symbol: "AUDIO_PLAY_500MS_LAST", icon: "play-before-end",
                      tip: qsTranslate("HikariSub.Audio", "Play 500ms before the end time"),
                      label: qsTranslate("HikariSub.Audio", "Play the last 500 ms") },
                    { id: "after", symbol: "AUDIO_PLAY_500MS_AFTER", icon: "play-after-end",
                      tip: qsTranslate("HikariSub.Audio", "Play 500ms after the end time"),
                      label: qsTranslate("HikariSub.Audio", "Play 500 ms after the end") },
                    { id: "toEnd", symbol: "AUDIO_PLAY_TO_END", icon: "play-to-end",
                      tip: qsTranslate("HikariSub.Audio", "Play to the end"),
                      label: qsTranslate("HikariSub.Audio", "Play to the end") },
                    { id: "beforeMark", symbol: "AUDIO_PLAY_BEFORE_MARK", icon: "play-before-mark",
                      tip: qsTranslate("HikariSub.Audio", "Play before the tag"),
                      label: qsTranslate("HikariSub.Audio", "Play before the mark") },
                    { id: "afterMark", symbol: "AUDIO_PLAY_AFTER_MARK", icon: "play-after-mark",
                      tip: qsTranslate("HikariSub.Audio", "Play after the tag"),
                      label: qsTranslate("HikariSub.Audio", "Play after the mark") }
                ]
                property string playAroundSetting: root.app.settings.value("audio.playAround")
                readonly property var playAround: playArounds.find(p => p.symbol === playAroundSetting) ?? playArounds[0]
                function choosePlayAround(symbol) {
                    root.app.settings.setValue("audio.playAround", symbol)
                    root.audio.runHotkey(symbol)
                }
                // A3: AUDIO_AUTO_COMMIT and AUDIO_NEXT_LINE_ON_COMMIT
                // (AudioBox::OnAutoCommit / OnNextLineCommit save the option;
                // legacy's commit never reads the second), now in Commit's menu.
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
                // AU2 (#268): the controls AU1 folded that the row shows
                // (audio.panelControls, the Customise menu). The extras in
                // the row's order, left to right: legacy's (AudioBox.cpp:
                // 160-193), the mark plays, the 500 ms plays and Play to end,
                // then Auto-commit, Next after commit and Go after Commit.
                property var panelControls: root.app.settings.value("audio.panelControls")
                readonly property var extraOrder: ["beforeMark", "afterMark", "before", "first", "last", "after", "toEnd",
                                                   "autoCommit", "nextCommit", "goto"]
                readonly property var playExtras: ["beforeMark", "afterMark", "before", "first", "last", "after", "toEnd"]
                // Classic: close to legacy's box (AU2-classic-arrangement);
                // Default: AU1's row.
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
                // AU1: the zoom readout, the horizontal stretch against the
                // default ("1.5×"), whole numbers from 10×.
                readonly property string magnificationText: {
                    const m = root.audio.magnification
                    //: The audio panel's zoom readout; %1 is the magnification
                    return qsTranslate("HikariSub.Audio", "%1×").arg(m >= 10 ? Math.round(m) : m.toFixed(1))
                }
                // AU1: Go to selection's button gave way to this chip, shown
                // only while none of the current Line's selection is in view,
                // on the side it lies beyond (AU1-go-to-selection-chip).
                // AUDIO_GOTO (B) keeps working everywhere.
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
                            // the arrow points to the Line
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
                // The box's buttons (legacy AudioBox's ButtonSizer). AU1
                // (#267): transport, Play around and the timing group on the
                // left; the mode switches, the toggles, zoom and the speaker
                // on the right. Below the width the whole row needs, the
                // switches and toggles fold into a ⋯ menu in their order.
                RowLayout {
                    id: audioButtons
                    objectName: "audioButtons"
                    visible: root.audio.loaded
                    height: visible ? implicitHeight : 0
                    spacing: 2
                    // legacy MainSizer: a 2-pixel spacer under the buttons
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: visible ? 2 : 0 }
                    // From the fixed sizes, not the right-hand items' own,
                    // which change as the row folds. KA2 (#277): Karaoke
                    // mode's row, with Tap, folds the toggles into ⋯ first
                    // (the default 640 px panel does), the switches when
                    // even that does not fit; Line mode folds them together.
                    readonly property bool folded: width < transportWidth(shownExtras)
                                                           + (root.audio.karaoke ? audioSwitches.modesWidth + 26 + spacing
                                                                                 : audioSwitches.fullWidth)
                                                           + audioTail.unfoldedWidth + 3 * spacing
                    readonly property bool togglesFolded: folded
                        || width < transportWidth(shownExtras) + audioSwitches.fullWidth + audioTail.unfoldedWidth + 3 * spacing
                    // KA1 (owner 2026-10-21): Auto split's arrow, after the
                    // toggles and before the switches; its commands are in ⋯
                    // while it is folded (the default panel folds it)
                    readonly property bool splitArrowFolded: folded
                        || root.audio.karaoke && width < transportWidth(shownExtras) + audioSwitches.modesWidth
                                                        + audioSwitches.splitArrowWidth + 26 + spacing
                                                        + audioTail.unfoldedWidth + 3 * spacing
                    // the row with the switches in ⋯ and no extra shown: the
                    // panel's least width
                    readonly property real foldedWidth: transportWidth([]) + audioTail.unfoldedWidth + 26 + 3 * spacing
                    // AU2: the extras Customise shows, in the row's order;
                    // `kept` the ones the row has room for. Once the switches
                    // are in ⋯, the extras that do not fit follow them from
                    // the right end (AU2-classic-arrangement).
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
                    // audioTransport's width with the extras in `extras`
                    // shown, from the fixed sizes (a button 26, a split
                    // button 39, a separator 5 with its margins).
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
                    // Legacy MappedButton's square buttons; a Shift+click maps
                    // the action's Audio hotkey instead (Hotkeys::OnMapHkey).
                    // K1: each shows the set's icon in place of legacy's
                    // bitmap; its text is not drawn, the tip stays its
                    // accessible name and tooltip. AU1: a checked button has
                    // an accent line under it, so its state is not its fill
                    // alone.
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
                    // A segment of a two-way switch: one of its choices.
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
                    // AU1: Line | Karaoke and Waveform | Spectrum
                    // (AU1-audio-mode-switches).
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
                    // The arrow of a split button and of a menu button.
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
                    // AU1: a split button: the face runs its action, the arrow
                    // opens `menu` under the face.
                    component AudioSplitButton: Row {
                        id: split
                        property string name
                        property string iconRole
                        property string symbol
                        property string altSymbol
                        property string tip
                        property string menuTip
                        property Menu menu
                        // KA1: a face that toggles (Auto split), and a narrower arrow
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
                    // A mark on Commit's face: an option that is on.
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
                    // KA2: a choice of several in a menu, with a radio mark
                    // (its circle, filled when chosen); it closes the menu.
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
                    // KA2: a muted heading over a menu's group of items.
                    component AudioMenuHeading: Label {
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 4
                        bottomPadding: 2
                        color: Theme.muted
                        font.bold: true
                        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                    }
                    // A play of Play around's menu, its hotkey beside it; a
                    // Shift+click maps the hotkey (AU1: as the old button did).
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
                        // A3, A4: AUDIO_PREVIOUS, AUDIO_PLAY, AUDIO_PLAY_LINE
                        // (two hotkeys each), AUDIO_STOP, AUDIO_NEXT
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
                        // A4: the 500 ms, to-end and mark plays in one split
                        // button (AU1-play-around-split); AU2: unless
                        // Customise hides it.
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
                        // AU2 (#268): the plays Customise shows as their own
                        // buttons, legacy's (AudioBox.cpp:160-176): the mark
                        // plays, then the 500 ms plays and Play to end.
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
                        // not twice when nothing sits between the transport and
                        // the timing group
                        AudioSeparator {
                            visible: audioPanel.shows("playAround") || audioPanel.playArounds.some(p => audioButtons.keeps(p.id))
                        }
                        // A3: AUDIO_LEAD_IN, AUDIO_LEAD_OUT, AUDIO_COMMIT
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
                        // AU1: Commit's arrow holds Auto-commit and Next after
                        // commit; marks on the face show which are on
                        // (AU1-commit-split).
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
                                // AU2: not while the option is a button of
                                // its own on the row
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
                                // both of Commit's keys, as the face's tip
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
                        // AU2: Commit's options and Go to selection as
                        // buttons after Commit (legacy's AutoCommit,
                        // NextCommit and Goto, AudioBox.cpp:180-215).
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
                    // AU1: the mode switches and the toggles. `fullWidth` is
                    // what they take unfolded, whether shown or not.
                    RowLayout {
                        id: audioSwitches
                        objectName: "audioSwitches"
                        visible: !audioButtons.folded
                        spacing: 2
                        // two switches, and Auto split and Tap in Karaoke
                        // mode (and Auto split's arrow); a separator and three
                        // toggles
                        readonly property real modesWidth: 2 * 54 + 2 * spacing + (root.audio.karaoke ? 26 + 39 + 2 * spacing : 0)
                        readonly property real splitArrowWidth: root.audio.karaoke ? 9 : 0
                        readonly property real fullWidth: modesWidth + splitArrowWidth + 5 + 3 * 26 + 4 * spacing
                        // A5: legacy AudioBox's KaraSwitch, as a switch
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
                        // A5: KaraMode, in Karaoke mode only; KA1 (#276, owner
                        // 2026-10-21): its arrow holds the split at the cursor
                        // and the \k and \kf switch
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
                            arrowWidth: 9 // narrower than the others (owner, 2026-10-21)
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
                                text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k").arg("\\kf") + "\t"
                                      + root.boundKeys("AUDIO_KARAOKE_KF", 4)
                                onTriggered: root.audio.switchLongSyllablesToKf()
                            }
                        }
                        // KA2-tap-to-time: Tap, in Karaoke mode only. The face
                        // turns tap mode on and off (AUDIO_TAP), the arrow
                        // holds how a key makes a syllable, the speed and
                        // the latency.
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
                            // KA5 (#280, KA5-onset-snap): dragged and tapped
                            // boundaries snap to vocal onsets
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
                                //: The Tap menu's Speed submenu; %1 is the speed in percent
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
                                //: The Tap menu's item; %1 is the tap latency in milliseconds
                                text: qsTranslate("HikariSub.Audio", "Calibrate latency…") + "\t"
                                      + qsTranslate("HikariSub.Audio", "%1 ms").arg(root.audio.tapLatency)
                                onTriggered: tapCalibrationWindow.openWindow()
                            }
                        }
                        // A2: SpectrumMode, as a switch
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
                        // A2: legacy AudioBox's AutoScroll and SpectrumNonLinear
                        // switches (each focuses the display, as legacy's
                        // handlers do); AV2 (#239): the video and audio
                        // cursors follow each other while playing.
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
                        // the separator, zoom, the speaker and Customise
                        readonly property real unfoldedWidth: 5 + audioZoomButton.implicitWidth + 2 * 26 + 3 * spacing
                        AudioSeparator {}
                        // AU1: the switches and toggles of a narrow panel;
                        // KA2: in Karaoke mode the toggles alone first.
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
                                // the switches' check items, while they are folded too
                                component FoldedItem: ShellMenuItem {
                                    checkable: true
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                component FoldedSeparator: MenuSeparator {
                                    visible: audioButtons.folded
                                    height: visible ? implicitHeight : 0
                                }
                                // AU2: the extras the row has no room for,
                                // first, in its order
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
                                // KA1 (#276): the Auto split arrow's commands, while it is folded
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
                                    text: qsTranslate("HikariSub.Audio", "Switch long syllables between %1 and %2").arg("\\k").arg("\\kf") + "\t"
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
                                // the Tap button's menu (Hold or Press, the
                                // speed, the latency), at ⋯ while it is folded
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
                        // AU1: the stretches and their link, folded from the
                        // slider column into one button with a readout
                        // (AU1-audio-slider-column-folded). Shift+wheel and
                        // Ctrl+wheel on the waveform still zoom.
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
                                // the horizontal slider zooms in to the right
                                // (legacy's vertical one had 0 at the top)
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
                                // K1: the set's link icon (legacy VerticalLink's
                                // button_link bitmap, AudioBox.cpp:123).
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
                        // AV1 (#238): the output device menu; AU1: with the
                        // volume above it, the level shown by the icon, and
                        // the wheel changing the volume.
                        AudioButton {
                            id: audioOutputButton
                            objectName: "audioOutputButton"
                            iconRole: root.audio.volume > 66 ? "volume" : root.audio.volume > 20 ? "volume-low" : "volume-quiet"
                            text: qsTranslate("HikariSub.Audio", "Volume and output device")
                            //: The audio panel's speaker button; %1 is the volume (1 to 100), %2 the device audio plays through now
                            tip: (root.app.audioOutputName.length
                                  ? qsTranslate("HikariSub.Audio", "Volume: %1\nOutput device: %2").arg(root.audio.volume).arg(root.app.audioOutputName)
                                  : qsTranslate("HikariSub.Audio", "Volume: %1").arg(root.audio.volume))
                                 //: The speaker button's tooltip, while the audio plays slowed; %1 is the speed in percent
                                 + (root.audio.speedPercent !== 100
                                    ? "\n" + qsTranslate("HikariSub.Audio", "Speed: %1%").arg(root.audio.speedPercent) : "")
                            down: audioOutputMenu.visible
                            onRun: audioOutputMenu.popup(audioOutputButton, 0, audioOutputButton.height)
                            // KA2-slowed-playback: the speed, while not 100%
                            CommitMark {
                                objectName: "audioSpeedMark"
                                anchors { right: parent.right; bottom: parent.bottom; rightMargin: 1; bottomMargin: 1 }
                                glyph: root.audio.speedPercent
                                visible: root.audio.speedPercent !== 100
                            }
                            // 5 a notch of 120. A touchpad or high-resolution
                            // wheel sends parts of a notch (or only pixels, 15
                            // a notch): they add up until a whole notch, and a
                            // turn the other way drops what was left over.
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
                        // AU2 (#268): Customise: each control AU1 folded as a
                        // button of its own, and the two arrangements.
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
                                // An item a tick shows or hides; the menu stays
                                // open (checkable).
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
                // Legacy AudioBox's accelerator table (AudioBox::SetAccels:
                // its window's bindings in the hotkey registry, O2), acting
                // while the focus is in the box; the registry routes the key
                // (HotkeysController::actionFor), so a remapped key acts and
                // the old one no longer does.
                // KA2-tap-to-time: while tap mode is on, Space, Backspace and
                // Esc are its keys (before VIDEO_PLAY_PAUSE's Space), their
                // presses and releases.
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
                // Legacy EditBox::LoadAudio focuses a newly made display.
                Connections {
                    target: root.audio
                    function onOpened(path, created) {
                        if (created)
                            root.showPanel(audioDock)
                    }
                }
            }
        }

        // TM1 (#232): the Original text panel. In translation mode the Line's
        // Original text field (lineText) is here, and the Line editor's field
        // is the Translated text; the translation buttons and toggles are in
        // its header (OriginalTextTools). It follows the editing target's
        // mode (placeOriginal).
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
                // EditBox's accelerator table covered both text fields: the
                // Editor bindings the field does not take run here too.
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
                // LE1: the strip at its narrowest (its groups fold into
                // More) or the Line inspector's narrow grid, with the
                // content's side margins; its width-dependent implicit
                // width is not used. In height, the strip and a line of each
                // text field shown: the rest scrolls.
                minimumBodySize: Qt.size(Math.max(editorStrip.minimumWidth, editorInspector.minimumWidth)
                                         + 2 * (editorColumn.x - 4),
                                         editorStrip.implicitHeight
                                         + (lineTextSlot.visible ? editorColumn.spacing + lineText.Layout.minimumHeight : 0)
                                         + (translationText.visible ? editorColumn.spacing
                                            + translationText.Layout.minimumHeight : 0))
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Editor", "Line editor: %1").arg(shell.editingTitle)
                                              : qsTranslate("HikariSub.Editor", "Line editor")
                // O2: EditBox's accelerator table covers the whole Line editor
                // (TabPanel::SetAccels): a key its other controls do not take
                // (the time and margin fields keep their editing keys) runs
                // the Editor binding; the text fields route their own first.
                // The commit keys stay the fields' own (E4: LineInspector runs
                // them with OnNewline's rule for the time fields).
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
                // A dock shorter than the Line editor's rows (translation mode
                // in the default 1280 x 800 layout) scrolls them, with a thin
                // bar beside them; nothing is cut off below the dock's edge.
                // The wheel and the bar scroll; a mouse drag stays the text
                // fields' selection, and touch flicks. The scrolled area
                // takes the whole panel with the body's margin inside it, so
                // a control at the panel's edge keeps its focus ring.
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
                    // Keyboard focus brings its control into view, and a text
                    // field keeps its caret in view while it is typed in.
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
                        const room = 1 // the focus ring's (FocusFrame: 1 wide, touching the control)
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
                        // An editable box's text field brings the whole box.
                        for (let p = item.parent; p && p !== editorColumn; p = p.parent) {
                            if (p.activeFocus && p.height <= height)
                                item = p
                        }
                        // A text field taller than the view keeps its caret in view.
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
                        x: editorScroll.width - width - 2 // inside the panel's border
                        y: 2
                        height: editorScroll.height - 4
                        width: editorScroll.barWidth
                        padding: 0
                        policy: editorScroll.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        focusPolicy: Qt.NoFocus
                        // A visible track in the boundary colour, as the
                        // visual tool rail's and the audio display's.
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
                        // LE1: 12 at the sides (the dock title's inset, as
                        // MuseScore's Properties panel), the body's 4 above
                        // and below, in the scrolled area; rows 6 apart.
                        x: 12
                        y: 4
                        width: editorScroll.contentWidth - 2 * x
                        spacing: 6
                        // The rows' own heights, or the dock's when it is taller
                        // (the text fields take the rest).
                        height: Math.max(editorScroll.height - 8, implicitHeight)

                        // LE1: the tag strip and the counters (LineEditorStrip).
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
                            // Its groups fold instead (not their widths' sum).
                            Layout.minimumWidth: minimumWidth
                        }
                        // TM1-translator-check-removed: legacy's "Translator mode"
                        // check box (BoxSizer5, EditBox.cpp:233-239, 308) is
                        // Subtitles > Translation mode and the Translation preset.

                        // LE1: with both text fields here (translation mode,
                        // the Original text panel closed) each is named above it.
                        Label {
                            objectName: "originalCaption"
                            visible: lineTextSlot.visible && root.editor.translationMode
                            text: qsTranslate("HikariSub.Editor", "Original text")
                            color: Theme.muted
                            Layout.bottomMargin: -4
                        }

                        // TM1: the Line's text field, the Original text in
                        // translation mode. While the Original text panel shows,
                        // the field is there (root.originalShown) and the Line
                        // editor's text field is the Translated text.
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
                        // TM1: the Original's runs, placed on the translation's words.
                        RunStrip {
                            editor: root.editor
                            shellRoot: root
                            translated: translationText
                            Layout.fillWidth: true
                        }
                        // A karaoke Line carries its leading tags without the
                        // timing, which differs with the translation's syllables.
                        Label {
                            objectName: "karaokeCarryNotice"
                            visible: root.editor.translationMode && root.editor.karaokeCarried
                            text: qsTranslate("HikariSub.Translation", "Karaoke line: the tags before its text are carried, the karaoke timing is not.")
                            color: Theme.muted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        // TM1: legacy's translation-mode buttons (EDITBOX_PASTE_*,
                        // EDITBOX_HIDE_ORIGINAL renamed Comment out original) and
                        // toggles are the Original text panel's header
                        // (OriginalTextTools) while it shows; LE1: the strip's
                        // translation group while it is closed.

                        // LE1: the editor's problem and what it did not apply,
                        // right under the text they concern.
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

                        // E4: the Line's fields (legacy BoxSizer2, below the text).
                        LineInspector {
                            id: editorInspector
                            objectName: "lineInspector"
                            editor: root.editor
                            hotkeys: root.hotkeys
                            Layout.fillWidth: true
                            onStyleEditRequested: style => styleManagerWindow.editStyle(style) // LE6
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
                minimumBodySize: Qt.size(240, grid.rowHeight * 3) // the header and two Lines
                title: shell.hasEditingTarget ? qsTranslate("HikariSub.Grid", "Editing: %1").arg(shell.editingTitle) : qsTranslate("HikariSub.Grid", "No document open")
                accessibleName: qsTranslate("HikariSub.Grid", "Grid")
                focus: true
                keyTarget: grid
                HikariGrid {
                    id: grid
                    objectName: "editingGrid"
                    anchors.fill: parent
                    focus: true
                    // Puts the Grid in the shell's accessibility tree: Qt Quick
                    // lists only items with an Accessible attachment; the
                    // Grid's own table interface (ui/line_grid_accessible.h)
                    // answers for it.
                    Accessible.role: Accessible.Table
                    model: shell.lines
                    onContentYChanged: root.app.setTargetScroll(Math.floor(contentY / rowHeight)) // P6
                    // Every gesture is a request; the application owns the selection (G1).
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
                    onFoldToggleRequested: id => root.app.toggleFold(id) // KP1
                    onGroupMenuRequested: (id, x, y) => {
                        groupMenu.description = id
                        groupMenu.at = Qt.point(x, y)
                        groupMenu.popup(grid, x, y)
                    }
                    // Legacy tree description menu (ContextMenuTree).
                    ShellMenu {
                        id: groupMenu
                        objectName: "groupMenu"
                        property var description: 0
                        property point at: Qt.point(0, 0) // where the menu was asked for, in the grid
                        ShellMenuItem { iconRole: "tree-add-lines"; objectName: "groupAddLines"; text: qsTranslate("HikariSub.Grid", "Add lines"); onTriggered: root.app.addLinesToGroup(groupMenu.description) }
                        ShellMenuItem { iconRole: "edit-copy"; objectName: "groupCopy"; text: qsTranslate("HikariSub.Grid", "Copy tree"); onTriggered: root.app.copyGroup(groupMenu.description) }
                        ShellMenuItem {
                            iconRole: "edit"
                            objectName: "groupRename"; text: qsTranslate("HikariSub.Grid", "Change description")
                            onTriggered: groupDescriptionPopover.edit(groupMenu.description, grid, groupMenu.at)
                        }
                        ShellMenuItem { iconRole: "tree-select"; objectName: "groupSelect"; text: qsTranslate("HikariSub.Grid", "Select tree lines"); onTriggered: root.app.selectGroup(groupMenu.description) }
                        ShellMenuItem { iconRole: "delete"; objectName: "groupDelete"; text: qsTranslate("HikariSub.Grid", "Delete"); onTriggered: root.app.removeGroup(groupMenu.description) }
                    }
                    // The Grid's accelerators (TabPanel::SetAccels): the fixed
                    // clipboard keys (GRID_COPY Ctrl+C, GRID_CUT Ctrl+X,
                    // GRID_PASTE Ctrl+V) first, then the Subtitles bindings
                    // (O2; GRID_DUPLICATE_LINES Ctrl+D by default) with the
                    // Editor and Video actions bound for the Grid and Video's
                    // play and seek bindings, before the application's shortcuts.
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
                            // SetAccMenu: the binding's keys after the tab.
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
                            objectName: "continuousPrevious"; text: qsTranslate("HikariSub.Grid", "Set times as a continuous (previous line)")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_PREVIOUS_LINE")) root.app.makeContinuous(true)
                        }
                        ShellMenuItem {
                            command: "GRID_MAKE_CONTINOUS_NEXT_LINE"
                            iconRole: "continuous-next"
                            objectName: "continuousNext"; text: qsTranslate("HikariSub.Grid", "Set times as a continuous (next line)")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_NEXT_LINE")) root.app.makeContinuous(false)
                        }
                        // Legacy "Split lines" (GRID_SPLIT_BY_*).
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
                        // V6: after "Split lines", as legacy's menu (SubsGrid.cpp:264-265).
                        ShellMenuItem {
                            command: "GRID_SELECT_VISIBLE_LINES"
                            iconRole: "select-visible"
                            objectName: "selectVisibleLines"; text: qsTranslate("HikariSub.Grid", "Select all lines visible on video")
                            onTriggered: if (!root.gridGesture(this, "GRID_SELECT_VISIBLE_LINES")) root.app.selectLinesVisibleOnVideo()
                        }
                        ShellMenuItem { command: "GRID_TREE_MAKE"; iconRole: "make-tree"; objectName: "makeTree"; text: qsTranslate("HikariSub.Grid", "Make tree"); onTriggered: if (!root.gridGesture(this, "GRID_TREE_MAKE")) root.app.makeGroups() }
                        // R2: legacy's in-Grid preview, here another tab in the reference tray (SubsGrid.cpp:277).
                        ShellMenuItem {
                            command: "GRID_SHOW_PREVIEW"
                            iconRole: "reference"
                            objectName: "showPreview"
                            readonly property string keys: root.boundKeys("GRID_SHOW_PREVIEW", 1)
                            text: qsTranslate("HikariSub.Grid", "Show subtitles preview") + (keys.length ? "\t" + keys : "")
                            enabled: gridMenu.visible && root.app.canShowPreview()
                            onTriggered: if (!root.gridGesture(this, "GRID_SHOW_PREVIEW")) root.app.showPreview()
                        }
                        // E3: GRID_PASTE_TRANSLATION and GRID_TRANSLATION_DIALOG.
                        ShellMenuItem {
                            command: "GRID_PASTE_TRANSLATION"
                            iconRole: "paste-translation"
                            objectName: "pasteTranslation"
                            text: qsTranslate("HikariSub.Grid", "Paste translation text")
                            enabled: gridMenu.canPasteTranslation
                            help: root.app.translationHint // E5-tlmode-needs-script-info: why it is disabled
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
                        // Legacy Filtering submenu (GRID_FILTER_*).
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
                        // Legacy "Hide columns" (GRID_HIDE_LAYER ... GRID_HIDE_WRAPS).
                        ShellMenu {
                            id: hideColumnsMenu
                            iconRole: "columns"
                            objectName: "hideColumnsMenu"
                            title: qsTranslate("HikariSub.Grid", "Hide columns")
                            // GR3 (Aegisub's convention): columns empty in
                            // the whole Document hide themselves; the same
                            // setting as Settings > Editor's.
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
                        // E6: SubsGrid's menu, "Delete text" before "Delete" (SubsGrid.cpp:285-286).
                        ShellMenuItem { command: "GLOBAL_REMOVE_TEXT"; iconRole: "clear"; objectName: "deleteText"; text: qsTranslate("HikariSub.Grid", "Delete text"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_TEXT")) root.app.deleteText() }
                        ShellMenuItem { command: "GLOBAL_REMOVE_LINES"; iconRole: "delete"; objectName: "deleteLines"; text: qsTranslate("HikariSub.Grid", "Delete lines\tShift+Del"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_LINES")) root.app.deleteLines() }
                        // Y8: SubsGrid's menu (SubsGrid.cpp:286-288).
                        MenuSeparator {}
                        ShellMenuItem {
                            command: "GLOBAL_OPEN_FONT_COLLECTOR"
                            iconRole: "font-collector"
                            objectName: "gridFontCollector"; text: qsTranslate("HikariSub.Grid", "Font collector"); enabled: root.shell.assColumns
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_OPEN_FONT_COLLECTOR")) root.openFontCollector()
                        }
                        // Y9: SubsGrid.cpp:289, enabled for a ".mkv" or ".ogm" video.
                        ShellMenuItem {
                            command: "GRID_SUBS_FROM_MKV"
                            iconRole: "extract-subtitles"
                            objectName: "gridSubsFromMkv"; text: qsTranslate("HikariSub.Grid", "Load subtitles from an MKV/OGM file")
                            enabled: root.matroska.available
                            onTriggered: if (!root.gridGesture(this, "GRID_SUBS_FROM_MKV")) matroskaSubtitles.begin()
                        }
                    }
                }
                // The Grid's empty state.
                Label {
                    objectName: "gridEmptyState"
                    anchors.centerIn: parent
                    visible: !shell.hasEditingTarget
                    text: qsTranslate("HikariSub.Grid", "No document open")
                    color: gridPanel.palette.placeholderText
                    Accessible.ignored: true // the panel's description says it
                }
            }
        }

        KDDW.DockWidget {
            id: referenceDock
            objectName: "referenceDock"
            uniqueName: "Reference"
            // The tray's dock names the Protected reference: unlike the
            // Grid's Document, no Document tab shows it.
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
                // R2: its own navigation, linked matching and legacy's preview menu.
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

        // F5: the Timing tool (legacy ShiftTimes panel).
        KDDW.DockWidget {
            id: timingDock
            objectName: "timingDock"
            uniqueName: "Timing"
            // One name for the tool: the menu's, the panel's and legacy's
            // ("Shift times"); the uniqueName keeps saved layouts.
            title: qsTranslate("HikariSub.TimeShift", "Shift times")
            Panel {
                id: timingPanel
                objectName: "timingPanel"
                dockName: "Timing"
                minimumBodySize: shiftTimesPanel.minimumBodySize // the form scrolls
                anchors.fill: parent
                title: qsTranslate("HikariSub.TimeShift", "Shift times")
                // TS1: the form and its run button.
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

        // F1: the Search tool (find and replace; docs/qt/ux/reviewed-surface-layouts.md B).
        KDDW.DockWidget {
            id: searchDock
            objectName: "searchDock"
            uniqueName: "Search"
            title: qsTranslate("HikariSub.Search", "Search")
            // TabWindow::SaveValues when the tool closes.
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

        // AM1: the Automation panel (the persistent manager tool,
        // docs/qt/automation.md), with its macros' shortcuts.
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

        // PU6 (#308): the Spellchecker panel (legacy's Spellchecker window).
        // Closing it ends the check.
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
                SpellCheckerPanel {
                    id: spellTool
                    objectName: "spellCheckerPanel"
                    anchors.fill: parent
                    focus: true
                    app: root.app
                    notices: root.notices
                    documentName: root.shell.editingTitle
                    onSettingsRequested: root.openSettings("lineEditor") // the dictionary
                }
            }
        }

        // TP1 (#274): the Styles panel, the editing target's Styles, beside
        // the video in the Typesetting preset.
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

        // KE2: the Template editor (docs/qt/karaoke-templater.md, "The
        // template editor"). Its previews run, and the video draws them, only
        // while it shows: open, and the current tab of its group.
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

        // NT1 (#302): the Log panel (legacy's Log window). A toast's Details
        // opens it at its entry; nothing else opens it by itself.
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
        // PU2 (#304): the Fix minor errors panel (legacy Multireplacer and
        // its Search results), shown and hidden by Edit › Fix minor errors.
        KDDW.DockWidget {
            id: misspellDock
            objectName: "misspellDock"
            uniqueName: "FixMinorErrors"
            title: qsTranslate("HikariSub.MisspellReplacer", "Fix minor errors")
            // MR->Show() read Rules.txt again at each showing. Not on the
            // passing open of the default arrangement, which closes it at
            // once: read rules are written back at exit (Codex review).
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
                    // DestroyDialogs (a changed program font): MR->Destroy().
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
            // D3: the horizontal panels keep a one-tab header even alone;
            // the Reference tray's toolbar sits in its header.
            Docking.setPanelHeader("Grid", true, null)
            Docking.setPanelHeader("Reference", true, referenceTray.toolbar)
            Docking.setPanelHeader("Search", true, null)
            Docking.setPanelHeader("Log", true, null)
            Docking.setPanelHeader("Spellchecker", true, null)
            Docking.setPanelHeader("Original", true, originalTools) // TM1
            Docking.setPanelHeader("Styles", true, stylesTool.toolbar) // TP1
            Docking.setPanelHeader("Template", false, templateTool.toolbar)
            root.defaultLayout()
        }
    }
    // Once the arrangement is laid out (its first frame), give the audio box
    // legacy's height whatever this platform's fonts make the panel chrome,
    // then keep that as the default (Reset layout) before a saved layout
    // replaces it. D3: the panels' minimum sizes come first, from their
    // laid-out content (Panel), so the default arrangement holds them.
    property bool arrangementSettled: false
    function settleArrangement() {
        if (root.arrangementSettled)
            return
        root.arrangementSettled = true
        for (const panel of root.panels)
            panel.reportMinimumSize()
        root.fitAudioBox()
        root.workspaceLayout.captureDefault()
        root.workspaceLayout.buildsArrangement("Typesetting") // TP1: when first chosen
        root.workspaceLayout.buildsArrangement("Karaoke") // KP1
        root.workspaceLayout.restoreSaved()
        root.placeOriginal() // TM1: a TLMode file opened before the first frame
        Docking.guardClose(root, root.admitClose)
        // Decision 63: the window is never smaller than the docked layout's
        // minimum, so no docked panel is cut off past its edge.
        Docking.followLayoutMinimum(dockingArea.uniqueName)
    }
    onFrameSwapped: settleArrangement()
    Timer { // without frames (a hidden window)
        interval: 1000
        running: !root.arrangementSettled
        onTriggered: root.settleArrangement()
    }
    // Completed layout operations are saved, not every drag (D1): a cheap
    // periodic check writes only when the arrangement changed.
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.workspaceLayout.save()
    }

    // WS4: the window's top rows under the title bar (SH1): the open
    // Documents' tabs, as in MuseScore and IDEs, then the notices, each
    // shown only while it has something to say.
    header: ColumnLayout {
        objectName: "windowTop"
        spacing: 0
        // P6: the open Documents' tabs (legacy Notebook's bar).
        DocumentTabBar {
            id: documentTabBar
            app: root.app
            Layout.fillWidth: true
            onCloseRequested: index => root.closeTab(index)
            // P9: the tab menu's Save, Save all and Close all tabs.
            onSaveRequested: id => root.saveSubtitles(id)
            onSaveAllRequested: root.saveAllTabs()
            onCloseAllRequested: tabCommands.confirmCloseAll()
        }
        // D1: a layout that could not be restored is named here, with the way back.
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
        // ADR 0019: a downloaded update that is ready to install.
        UpdateReadyBar {
            id: updateReadyBar
            updates: root.updates
            Layout.fillWidth: true
            // The normal quit follows: its review of unsaved work, then the
            // installer. A cancelled review keeps the update for later.
            onRestartRequested: {
                root.updates.installer.restartNow()
                root.close()
            }
        }
        // PU1-startup-cards: the startup offer while a Document is open (files
        // from the command line); Home shows it as a card.
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
        // PU1-mkv-notices: subtitles read from an MKV (legacy's modal
        // ProgresDialog "Loading subtitles from Matroska.": the gauge, "Time
        // elapsed", Cancel), a bar while the window stays usable.
        InlineBar {
            objectName: "mkvProgress"
            // With the tab the read started on (it goes on while another
            // tab, or Home, shows).
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
        // PU1-disk-change-bar: the editing target's file was changed by another
        // program (legacy's modal "Subtitles were modified by another program.
        // Reload?"): Reload reads it again, Compare opens it as the protected
        // reference and compares, Keep mine ends the bar and accepts the
        // change (a save then writes over the file).
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
                    // The bar stays when the file can't be read now.
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
                    // A file that can't be read now can't be kept over: the
                    // bar stays (Save as… still saves the work).
                    if (root.app.acceptFileOnDisk(diskChangeBar.tab.id))
                        root.endDiskChange(diskChangeBar.tab.id)
                    else
                        root.notify("warning", qsTranslate("HikariSub.Documents", "Keep mine"),
                                    qsTranslate("HikariSub.Editor", "The file changed on disk since it was opened. Reload it, or save elsewhere."))
                }
            }
        }
        // PU1-session-entries-bar: what a restore could not find for this tab,
        // one bar each, with Retry, Relink… and Remove (they stay until then).
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

    // P10: legacy's status bar, the window's bottom row. Its first field is
    // help and progress text (Automation's set_status_text, menu help,
    // "Autosave"), never the editing target, which the Document tab and the
    // window title name.
    footer: StatusBar {
        id: statusStrip
        shell: root.shell
        fieldsController: root.statusBar
        selectionText: root.shell.selectionStatus
        // The last save's message only: the tab's modified mark says
        // "Modified" (said once, visual-language.md).
        saveText: root.editor.saveStatus
    }
    // PK1 (#301): the toasts, at the bottom right of the main window above
    // the status bar, or of the fullscreen video window (above its pinned
    // bar) while it is open. root.notices.post(severity, title, text, options);
    // C++ gives them through LogController::notice (NT1). NT1: beside the
    // window's popup overlay and above it, so a modal dialog's dimmer does
    // not hide them (Settings' warnings).
    // The editing target's row of app.tabs ({} without one).
    function currentTab() {
        const tabs = root.app.tabs
        return root.app.currentTab >= 0 && root.app.currentTab < tabs.length ? tabs[root.app.currentTab] : ({})
    }
    // PU1-picker-titles: the folder a file picker was last left in
    // ("subtitles", "sessions", "video", "audio", "keyframes"), kept in
    // workspace.pickerFolders; "" when none was.
    function pickerFolder(picker) {
        const rows = root.app.settings.value("workspace.pickerFolders") || []
        const row = rows.find(r => r.startsWith(picker + "\t"))
        return row ? row.slice(picker.length + 1) : ""
    }
    // A picker's own start (the Document's or the video's folder, legacy's
    // rules), else the folder that kind of picker was last left in.
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
    // The tab name of the Document `id` (0: the editing target's).
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
        // AD3 (#312): a macro run's notice and progress card, under the toasts.
        AutomationProgress {
            id: automationProgress
            width: parent ? parent.width : implicitWidth
            automation: root.automation
            onOpenRun: runId => root.openAutomationRun(runId)
            // Back to the panel F6 came from; with none (every panel
            // closed), to the window itself.
            onFocusReturned: panel => {
                if (panel)
                    root.focusPanel(panel, Qt.OtherFocusReason)
                else
                    root.contentItem.forceActiveFocus(Qt.OtherFocusReason)
            }
        }
    }
    readonly property alias notices: notices
    // NT1: a toast's Details (and "N more notices") opens the Log panel,
    // out of fullscreen (as the fullscreen's Audio settings).
    Connections {
        target: root.log
        function onRevealRequested() {
            if (root.videoFullscreen.active)
                root.videoFullscreen.leave()
            root.showPanel(logDock)
        }
    }
    // NT1 (#302): the message box for the notices that need an answer
    // (root.messages.ask(options, done), MessageHost.qml).
    MessageHost {
        id: messages
        objectName: "messages"
        settings: root.app.settings
    }
    readonly property alias messages: messages
    // WS3: Save layout as… and Manage layouts… (the switcher's and View > Layout's).
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

    // KA2 (#277): Calibrate latency…, from the audio panel's Tap menu.
    TapCalibrationWindow {
        id: tapCalibrationWindow
        audio: root.audio
    }

    // Legacy HistoryDialog (PU2: HistoryWindow.qml).
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

    // Legacy HikariSubFrame::Save: "Save subtitle file" for the Document's
    // format, starting at its file (or the video's, with the video name).
    // P9: for any tab's Document `id` (none: the editing target), and Save
    // all's queue of Documents that need the dialog, one after another.
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
        return true // it asks
    }
    property var saveQueue: []
    function saveAllTabs(overDisk) {
        // Only the saves written over their own file ask: a Save As or a
        // read-only file's question doesn't write it.
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
        // PU1-picker-titles: named for the Document; its format first
        // (the extension is added to a name without it).
        saveAsDialog.title = qsTranslate("HikariSub.Documents", "Save “%1” as").arg(root.tabTitle(id ?? 0))
        saveAsDialog.nameFilters = [v.filter, qsTranslate("HikariSub.Documents", "All files (*)")]
        if (v.folder.toString() !== "")
            saveAsDialog.currentFolder = v.folder
        else if (root.pickerFolder("subtitles").length > 0)
            saveAsDialog.currentFolder = root.pickerFolder("subtitles")
        saveAsDialog.selectedFile = v.file // none: not the last Document's
        saveAsDialog.open()
    }
    FileDialog {
        id: saveAsDialog
        objectName: "saveAsDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTranslate("HikariSub.Documents", "Subtitle file ") + "(*.ass)"]
        property var document: 0 // P9: the Document saved (0: the editing target)
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            const result = root.app.saveChosenFor(document, selectedFile)
            if (result === "readonly") {
                root.warnReadOnly(document, root.app.localPath(selectedFile).replace(/^.*[\\/]/, ""))
            } else {
                // PU1-disk-change-bar: saved under another name, the bar
                // about the file it came from goes once that write is done.
                const id = document === 0 ? root.currentTab().id : document
                if (result === "" && root.changedOnDisk[id] === true)
                    root.savingAway = Object.assign({}, root.savingAway, { [id]: true })
                matroskaSubtitles.saveDone() // Y9: the load waits for the Save dialog
                root.saveNext()
            }
        }
        onRejected: {
            matroskaSubtitles.saveDone()
            root.saveNext() // legacy SaveAll goes on with the next tab
        }
    }
    // Y9: GRID_SUBS_FROM_MKV's question, track chooser and progress.
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
    // PU1-message-boxes on NT1's host: a Save onto a read-only file
    // (legacy's OK-only "Warning", whose OK opened the Save dialog again,
    // with no way out but it): Save as… opens it, Cancel goes on as a
    // cancelled Save dialog does.
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

    // PU1-disk-change-bar: a save while a Document's changed-on-disk bar
    // shows asks first (legacy's question was modal, answered before any
    // save); Save over it accepts the change, ends those bars and saves
    // (otherwise the save is refused: the file is not what was read).
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
                // A file that can't be read now (gone, unreadable) can't be
                // saved over: its bar stays, and nothing is saved.
                const unread = id === "saveOver" ? ids.filter(marked => !root.app.acceptFileOnDisk(marked)) : []
                if (id === "saveOver" && unread.length === 0) {
                    for (const marked of ids)
                        root.endDiskChange(marked)
                    // Y9: an MKV load waiting for this save goes on once it
                    // is written, and is cancelled if it fails.
                    matroskaSubtitles.awaitWrite(ids, root.currentTab().id)
                    if (save() === false) // refused: nothing is being written
                        matroskaSubtitles.saveCancelled()
                } else if (id === "saveOver") {
                    root.notify("warning", qsTranslate("HikariSub.Documents", "Save"),
                                qsTranslate("HikariSub.Editor", "The file changed on disk since it was opened. Reload it, or save elsewhere."))
                    if (cancelled)
                        cancelled()
                } else if (cancelled) {
                    cancelled()
                }
                // Y9: nothing saved: a waiting MKV load is cancelled and the
                // work stays.
                if (id !== "saveOver" || unread.length > 0)
                    matroskaSubtitles.saveCancelled()
            })
        }
    }

    // P9: Close all tabs' question and a video's same-named subtitles.
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

    // The accepted close review (P1), on the popup kit (PU1-unsaved-window):
    // a window with every affected Document, or a message box for one.
    // Nothing closes until every save is acknowledged as written.
    UnsavedChanges {
        id: closeReview
        objectName: "unsavedChanges"
        app: root.app
        updates: root.updates
        settings: root.app.settings
        pickerFolder: picker => root.pickerFolder(picker)
        rememberPickerFolder: (picker, file) => root.rememberPickerFolder(picker, file)
        confirmOverDisk: (ids, then, cancelled) => {
            // The review writes each one to its own file (an Untitled one,
            // with no bar, to the file picked).
            const marked = ids.filter(id => root.changedOnDisk[id] === true)
            if (marked.length === 0)
                then()
            else
                diskSaveQuestion.confirm(marked, then, cancelled)
        }
    }

    // Automation windows (S1): the fixed script dialog and picker, the
    // manager tool and the progress window (legacy LuaProgressDialog).
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
        // P9; V5: from the fullscreen window without the subtitles question.
        onAccepted: {
            root.rememberPickerFolder("video", selectedFile) // PU1-picker-titles
            if (root.videoFullscreen.active)
                root.openVideoUnasked(root.app.localPath(selectedFile))
            else
                tabCommands.openVideoFile(root.app.localPath(selectedFile))
        }
        // V3: the subtitles' folder, else the latest recent video's (else
        // where the video picker was last left, PU1-picker-titles)
        function show() {
            currentFolder = root.startFolder("video", root.app.videoDialogFolder())
            open()
        }
    }
    // V3: GLOBAL_OPEN_DUMMY_VIDEO and the folder walk's question. PU4: the
    // window made at its first opening (PK1: every window made at the start
    // costs the program).
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
    // TM1: the Translation preset's question and Translation mode's turn-off
    // confirmation (Subtitles > Translation mode).
    TranslationModeQuestion {
        id: translationModeQuestion
        app: root.app
    }
    TranslationModeOffConfirm {
        id: translationModeOffConfirm
        app: root.app
    }

    // V4: VIDEO_ASPECT_RATIO (PU4: a popover from the context menus, a
    // compact window from the hotkey).
    AspectRatioDialog {
        id: aspectRatioDialog
        view: root.videoView
        video: root.video
        settings: root.app.settings
    }

    // V5: the fullscreen video window (legacy Fullscreen). What it asks
    // shows in it, as legacy parented its dialogs to m_FullScreenWindow
    // (VideoBox.cpp:895, 907, 1087, 1103).
    VideoFullscreen {
        id: videoFullscreenWindow
        shell: root
        controller: root.videoFullscreen
        onOpenVideoRequested: videoDialog.show()
        onOpenSubtitlesRequested: openDialog.show()
        onAspectRatioRequested: aspectRatioDialog.openAtCursor(videoFullscreenWindow.contentItem)
        onFileQuestionRequested: next => videoFileQuestion.ask(next, videoFullscreenWindow)
        // FS1: the output menu's "Audio settings…" opens the dialog in the main window.
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
    // SetFullscreen(monitor) with another monitor than 0 hides the docked
    // video while fullscreen lasts (m_IsOnAnotherMonitor, VideoBox.cpp:845-849)
    // and leaving shows it again (761-766).
    property bool videoDockClosedForFullscreen: false
    // Where the picture is (Main's videoStage): moved once the dock is
    // closed, as closing the dock takes its content back into the panel.
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
            // The picture moved to another window: a frame submitted just
            // before is refused there (the surface changed before upload),
            // so the shown frame is submitted again once the move is done.
            Qt.callLater(() => root.video.attachPresenter(presenter))
        }
    }

    FileDialog {
        id: openDialog
        objectName: "subtitlesOpenDialog"
        title: qsTranslate("HikariSub.Documents", "Open subtitles") // PU1-picker-titles
        // Where the subtitle pickers were last left.
        function show() {
            const folder = root.pickerFolder("subtitles")
            if (folder.length > 0)
                currentFolder = folder
            open()
        }
        // Legacy's second filter, "Videos with embedded subtitles". KA6:
        // word-timed lyrics, imported as karaoke Lines, after the subtitles.
        nameFilters: [qsTranslate("HikariSub.Documents", "Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"),
                      qsTranslate("HikariSub.Documents", "Word-timed lyrics (*.lrc *.ttml)"),
                      qsTranslate("HikariSub.Documents", "Videos with embedded subtitles (*.mkv *.ogm)"),
                      qsTranslate("HikariSub.Documents", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("subtitles", selectedFile)
            root.openChosenSubtitles(root.app.localPath(selectedFile))
        }
    }

    // GRID_SET_NEW_FPS (legacy FPSDialog): the subtitles' FPS and the new
    // one (PU4: a compact window, made at its first opening).
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

    // The column choice for Copy columns / Paste columns (PU2: ColumnsWindow.qml).
    ColumnsWindow {
        id: columnsWindow
        app: root.app
        settings: root.app.settings
    }

    // D1: every drag placement from the keyboard, and numeric resizing.
    // PU7 (#309): a compact window on DG1's frame, the panel, then a Move
    // section and a Resize section, each with its own button; it stays open
    // to move or resize again.
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
                // The docks themselves, and the shown text read from the dock
                // while the window shows. A dock's title reads empty until the
                // engine has made its dock widget, and the dock does not notify
                // the change, so text taken earlier stayed empty (D1 gate).
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

    // A1: legacy OpenAudioInTab's dialog and filter.
    FileDialog {
        id: audioDialog
        objectName: "audioDialog"
        title: qsTranslate("HikariSub.Audio", "Choose audio file")
        nameFilters: [qsTranslate("HikariSub.Audio", "Audio and video files") + " (*.wav *.w64 *.flac *.ac3 *.aac *.ogg *.mp3 *.mp4 *.m4a *.mkv *.avi)",
                      qsTranslate("HikariSub.Audio", "All files") + " (*)"]
        onAccepted: {
            root.rememberPickerFolder("audio", selectedFile) // PU1-picker-titles
            root.audio.openAudioUrl(selectedFile)
        }
    }
    // A1: legacy HikariListBox "Choose the track" (ProviderFFMS2::Init with
    // several audio tracks), on PK1's list chooser in its compact window:
    // the first row chosen, Open or a double click takes a row, Cancel opens
    // no audio.
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
        title: qsTranslate("HikariSub.Video", "Choose keyframes file") // TI1-keyframes-title
        nameFilters: [qsTranslate("HikariSub.Video", "Keyframes file (*.txt *.pass *.stats *.log)"), qsTranslate("HikariSub.Video", "All files (*)")]
        onAccepted: {
            root.rememberPickerFolder("keyframes", selectedFile) // PU1-picker-titles
            // V3: the recent keyframes take the file too (SetRecent(3))
            const problem = root.app.openKeyframesFile(selectedFile.toString())
            if (problem.length > 0)
                root.notices.post("error", qsTr("Open keyframes"), problem)
        }
        // V3: the video's folder, else the latest recent keyframes' (else
        // where the keyframes picker was last left, PU1-picker-titles)
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
    // Legacy TLDialog (PU2: TranslationShiftWindow.qml).
    TranslationShiftWindow {
        id: translationShiftWindow
        app: root.app
        settings: root.app.settings
    }
    // GLOBAL_SHIFT_TIMES' question (runShiftTimes).
    HidableQuestion {
        id: shiftQuestion
        objectName: "shiftQuestion"
        messages: root.messages
        questionId: "shiftOneTime"
        // The editing target it was asked for (App.editingTargetId).
        property var askedFor: 0
        property Connections targetChanges: Connections {
            target: root.app
            function onTabsChanged() {
                if (shiftQuestion.pending.length > 0 && root.app.editingTargetId() !== shiftQuestion.askedFor)
                    shiftQuestion.withdraw()
            }
        }
    }
    // P8: legacy UpdateChecker. Only the menu's check reports "up to date"
    // and failures; the automatic one speaks only for a newer release.
    // ADR 0019: an update that installs offers "Download and install" and,
    // once downloaded, the ready bar; PU7 (#309): an AUR copy gets the same
    // window, with its command.
    Connections {
        target: root.updates
        function onFinished(outcome, release, interactive) {
            if (outcome === "available") {
                // The automatic check's window takes no keyboard (DG1).
                updateAvailable.showRelease(release, !interactive)
            } else if (outcome === "current") {
                // NT1 (#302): toasts, titled with the command (DG1-update-titles).
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
    // DG1 (#222): About, Credits and the update offers are windows of their
    // own on the dialog frame (DialogWindow.qml), modeless.
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
    }
    CreditsDialog {
        id: creditsDialog
        app: root.app
        settings: root.app.settings
    }
    // Y5, PU6 (#308): conversion with the approved loss preview
    // (C02-loss-preview), Resample subtitles and ASS file properties
    // (ConversionWindow, ResampleWindow, ScriptPropertiesDialog), each made
    // at its first opening.
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
    // False for a Document that is not ASS.
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
    // Y4, PU6: legacy SubsMismatchResolutionDialog ("Incompatible
    // resolution") as a message box titled with the command that found it:
    // both sizes on one line, the choices as buttons (Resample the default,
    // Keep as is on Esc, Resample and stretch only when the aspect ratio
    // differs), Don't ask again legacy's "Disable warning".
    function askResolutionMismatch(sizes) {
        const choices = [{ id: "keep", text: qsTranslate("HikariSub.Conversion", "Keep as is"), role: "reject" },
                         { id: "only", text: qsTranslate("HikariSub.Conversion", "Change resolution only"), role: "action" }]
        if (sizes.videoWidth / sizes.subsWidth !== sizes.videoHeight / sizes.subsHeight)
            choices.push({ id: "stretch", text: qsTranslate("HikariSub.Conversion", "Resample and stretch"), role: "action" })
        choices.push({ id: "resample", text: qsTranslate("HikariSub.Conversion", "Resample"), role: "accept", isDefault: true })
        // The answer acts on the Document and the video it shows: with
        // another tab active by then, or another video or resolution (the box
        // is modal to its window only), it does nothing.
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
                      .arg(sizes.videoWidth).arg(sizes.videoHeight).arg(sizes.subsWidth).arg(sizes.subsHeight),
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
    // FR1 (#363): Getting started, by itself at the first start (and at the
    // next one when it was left unanswered), else from Help.
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
        // #393: from Help, Skip changes nothing and says nothing.
        onSkipped: if (gettingStartedDialog.due) root.notices.post("info", qsTranslate("HikariSub.GettingStarted", "Default settings in use"),
                                                                  qsTranslate("HikariSub.GettingStarted", "Help › Getting started opens the setup again."))
    }
    // PF1 (#225): the Settings window, made at its first opening (a window
    // made at the start slows every start, PK1).
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
                colourPicker: root.colourPicker
                tagButtons: root.tagButtons
            }
        }
    }
    function settingsWindow() {
        settingsLoader.active = true
        return settingsLoader.item
    }
    // `page`: a page's id ("audio", "hotkeys"); without one, the page shown
    // last. `filter`: the Hotkeys page's filter.
    function openSettings(page, filter) { settingsWindow().openDialog(page, filter) }
    // Y8: the font collector, PU6: a window on DG1's frame made at its first
    // opening (as KA7's); a double click on a Style in its log opens the
    // Style editor (StyleStore::ShowStyleEdit).
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
            styleManagerWindow.showFor(style) // SY1: the editor is on it
        }
    }
    SelectLinesDialog {
        id: selectLinesDialog
        app: root.app
        notices: root.notices
        // DestroyDialogs (a changed program font): SaveOptions, then gone.
        Connections {
            target: root.app
            function onSelectLinesDestroyed() {
                if (selectLinesDialog.visible)
                    selectLinesDialog.close()
            }
        }
    }
    // F3: the editor's "Fix suggestions" list (the Spellchecker is a panel,
    // PU6; legacy's spelling message boxes are toasts, NT1).
    // KA7: the Karaoke check window, made at its first opening, as PK1's
    // windows are: every window made at the start costs it (on Windows a
    // native event filter apiece).
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
    // KA3: the Kanji timer window, made at its first opening too.
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
    // W1: DirectShow could not open the video: legacy VideoBox::LoadVideo's
    // HikariMessageBox is a warning toast (NT1, application_directshow.cpp).
    // EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK's "Fix suggestions", on PK1's list
    // chooser as a popover at the misspelled word: a click or Enter replaces
    // it, Esc or a press outside leaves it.
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
    // PU3 (#305): the font picker, and the program's one catalog window and
    // one "Add fonts from subtitles" window (FontWindows), which the picker
    // and the Style editor open.
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
    // SY2: the one colour picker, anchored to the colour button.
    ColourPopover {
        id: colourPopover
        picker: root.colourPicker
    }
    // Y7: EditBox::AllColorClick (EditBox.cpp:862-919). SY2: a left click and
    // the hotkey open the colour popover, a right click opens it with the
    // eyedropper started (legacy's simple "Color picker" took the pointer at
    // once); COLORPICKER_SWITCH_CLICKS swaps them.
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

    // Legacy TreeDialog ("Tree description"), PU2 (#304): a popover under
    // the line group's row, opened from its menu's Change description, as
    // PK1's spelling suggestions open at the word (PU2-group-description).
    // Enter, Rename or a press outside renames; Esc changes nothing.
    Popover {
        id: groupDescriptionPopover
        objectName: "groupDescriptionPopover"
        title: qsTranslate("HikariSub.Grid", "Line group description")
        showTitle: true
        initialFocus: groupDescription
        property var description: 0
        property bool titled: false
        // Opens under the row at `at` in `grid` (the menu's place).
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
        // The group is in the tab it was opened from: with the Grid floating
        // the main window's tabs stay usable, and another editing target
        // closes it as Esc does, changing nothing (Codex review).
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

    // G56-contiguity: a command that would break a Line group is refused.
    // PU2 (#304): a message box to remove the group (its description Line
    // is deleted) or cancel; without a group to remove, a warning toast
    // (PU2-group-break).
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
        // The group is in the tab that refused: with the Grid floating the
        // main window's tabs stay usable, and another editing target closes
        // the box unanswered (Codex review), as Translation mode's questions.
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
        if (root.app.gettingStartedDue())
            Qt.callLater(() => gettingStartedDialog.openSetup())
    }
    // P3, P4 and P9 in one window (PU1-autosaves-window): the work left by a
    // session that did not close cleanly, the previous version's autosaves,
    // and removing them. It no longer opens by itself at the start: the
    // Home page's crash card or the startup bar offers it.
    Autosaves {
        id: autosaves
        objectName: "autosaves"
        app: root.app
        settings: root.app.settings
        onBundlesDeleted: sessionWindows.offer.recountAutosaves()
    }

    // O2: the shortcut editor. An installed binding as legacy text ("Ctrl-D"), following installs.
    function boundKeys(symbol, window) {
        return root.hotkeys.keys ? root.hotkeys.accelOf(symbol, window) : ""
    }
    // A mapped button's tooltip (MappedButton::SetToolTip).
    function mappedTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shortcut can be set using Shift + Click")
    }
    // A bitmap button's tooltip (BitmapButton::SetToolTip): its binding in
    // its window, then the gesture.
    function bitmapTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTranslate("HikariSub.Hotkeys", "Shortcut can be set using Shift + Click")
    }
    // Hotkeys::OnMapHkey (Hotkeys.cpp:466-540): the gesture maps the
    // action's hotkey instead of running it; the bindings are installed and
    // saved at once. `kind`:
    // - a menu item (omitted, "menu"): Shift alone, with the window choice
    //   (HikariSubFrame::OnMenuSelected/OnMenuSelected1, the Grid's menu);
    // - "macro": Shift alone, no window choice (Automation.cpp:1376-1379);
    // - a mapped button (true, "mapped"): Shift held, no window choice; with
    //   `two` (MappedButton's twoHotkeys) Ctrl held maps the second hotkey,
    //   the main id - 10 (O2-second-hotkey; legacy computed that id and then
    //   mapped the main one, MappedButton.cpp:437-445);
    // - "bitmap": the video panel's buttons (BitmapButton.cpp:97-106), Shift
    //   held, with the window choice.
    function hotkeyGesture(symbol, window, kind, two) {
        const mapped = kind === true || kind === "mapped"
        const held = mapped || kind === "bitmap"
        const shift = root.hotkeys.shiftClicked(!held)
        const second = !shift && mapped && two && root.hotkeys.ctrlClicked()
        if (!shift && !second) {
            // AN1 (#404): the command runs from a menu or a button (a key's
            // run reaches here too, and counts as the key's).
            if (root.keyRuns === 0)
                root.analytics.commandUsed(symbol, "pointer")
            return false
        }
        const target = root.hotkeys.gestureTarget(symbol, second)
        hotkeyMapping.capture(target.name, window ?? 0, !(mapped || kind === "macro"), (accel, type) => {
            const conflict = root.hotkeys.gestureConflict(target.id, accel, type)
            if (conflict.message === undefined)
                root.hotkeys.gestureMap(target.id, target.name, accel, type, "cancel")
            else
                hotkeyMapping.ask(conflict, answer => root.hotkeys.gestureMap(target.id, target.name, accel, type, answer))
        })
        return true
    }
    // SubsGrid::ContextMenu (SubsGrid.cpp:294-302): Shift with an id from
    // 4000 maps its hotkey for the Subtitles window (with the window
    // choice); a checkable item is not switched then.
    function gridGesture(item, symbol) {
        if (!root.hotkeyGesture(symbol, 1))
            return false
        if (item.checkable)
            item.toggle()
        return true
    }
    // A menu item's command when it is enabled (legacy checks the item's
    // state for an accelerator: OnMenuSelected's OnMenuOpened, OnMenuSelected1).
    function runItem(item) {
        if (item.enabled)
            item.triggered()
    }
    // The Global window's ids the menus' actions and items run (OnMenuSelected).
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
        GLOBAL_AUTOMATION_REPEAT_LAST_RUN: repeatRunAction, // AD2
        GLOBAL_PLAY_PAUSE: playPauseAction,
        GLOBAL_PREVIOUS_FRAME: previousFrameAction, GLOBAL_NEXT_FRAME: nextFrameAction,
        GLOBAL_SET_VIDEO_AT_START_TIME: goToStartAction, GLOBAL_SET_VIDEO_AT_END_TIME: goToEndAction,
        GLOBAL_SET_START_TIME: setStartTimeAction, GLOBAL_SET_END_TIME: setEndTimeAction, // V6
        GLOBAL_GO_TO_NEXT_KEYFRAME: nextKeyframeAction, GLOBAL_GO_TO_PREVIOUS_KEYFRAME: previousKeyframeAction,
        GLOBAL_SET_AUDIO_FROM_VIDEO: setAudioFromVideoAction, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: setAudioMarkFromVideoAction,
        GLOBAL_VIDEO_ZOOM: videoZoomAction, GLOBAL_RESET_VIDEO_ZOOM: resetVideoZoomAction, // V4
        GLOBAL_VIDEO_RENDER_COST: renderCostAction, // KE1
        GLOBAL_OPEN_SUBS: openAction, GLOBAL_OPEN_VIDEO: openVideoAction, GLOBAL_OPEN_KEYFRAMES: openKeyframesAction,
        GLOBAL_OPEN_AUTO_SAVE: openAutoSaveAction,
        GLOBAL_OPEN_DUMMY_VIDEO: dummyVideoAction, // V3
        GLOBAL_DELETE_TEMPORARY_FILES: removeTemporaryAction, GLOBAL_SETTINGS: settingsAction,
        GLOBAL_ABOUT: aboutAction, GLOBAL_HELPERS: creditsAction, GLOBAL_HELP: userGuideAction,
        GLOBAL_GETTING_STARTED: gettingStartedAction,
        GLOBAL_ANSI: reportIssueAction, GLOBAL_CHECK_FOR_UPDATES: checkForUpdatesAction,
        GLOBAL_EDITOR_HIDE_TAGS: hideEditorTagsAction, // LE3
        GLOBAL_TRANSLATION_MODE: translationModeAction, // TM1
        // P6: OnPageClose (the rewrite's File > Close).
        GLOBAL_CLOSE_PAGE: closeAction
    }
    }
    function globalItems() {
        return {
        GLOBAL_OPEN_SPELLCHECKER: checkSpellingItem, GLOBAL_OPEN_ASS_PROPERTIES: assPropertiesItem,
        GLOBAL_OPEN_STYLE_MANAGER: styleManagerItem, GLOBAL_OPEN_SUBS_RESAMPLE: resampleItem,
        GLOBAL_OPEN_FONT_COLLECTOR: fontCollectorItem, // Y8
        GLOBAL_SHOW_SHIFT_TIMES: showShiftTimesItem, GLOBAL_SHIFT_TIMES: runShiftTimesItem,
        GLOBAL_LOAD_EXTERNAL_SESSION: loadSessionFileItem, GLOBAL_SAVE_EXTERNAL_SESSION: saveSessionFileItem,
        GLOBAL_LOAD_LAST_SESSION: loadLastSessionItem,
        GLOBAL_CHECK_KARAOKE: checkKaraokeItem, // KA7
        GLOBAL_KANJI_TIMER: kanjiTimerItem, // KA3
    }
    }
    // DX1 (#367): where each command is in the menus, for the docs export:
    // symbol => ["File > Save", ...], the menu bar's places first, then the
    // Grid's menu's ("Grid menu > Insert > Insert before"). The titles as
    // shown, without mnemonics or the keys after a tab.
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
        walk(gridMenu, ["Grid menu"]) // (the export is in English)
        return out
    }
    // The menu bar's items show their Global binding at the right (legacy
    // SetAccMenu wrote it after a tab), refreshed when the bindings change.
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
    // KB3: each command a key reaches is reported (HotkeysController::ran,
    // the hotkey audit's record); rehearsing, it is only reported.
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
    // AN1 (#404): while a key's command runs, the menu actions it triggers
    // are the key's (Analytics hears it from HotkeysController::ran).
    property int keyRuns: 0
    function byKey(run) {
        ++root.keyRuns
        try {
            return run()
        } finally {
            --root.keyRuns
        }
    }
    // The frame's handlers of the Global window's bindings (HikariSubFrame:
    // OnMenuSelected, OnMenuSelected1, OnChangeLine, OnDelete, OnPageChange,
    // OnPageAdd, OnPageClose, OnAudioSnap, OnUseWindowHotkey), reached from
    // the shell's table and from a Global id bound for a panel. An id below
    // 5000 runs in its own window (OnUseWindowHotkey,
    // HikariSubFrame.cpp:1802-1814). A Global action the rewrite does not
    // have yet still takes its key, as legacy's accelerator does.
    function globalHotkey(symbol) {
        const field = root.editedField()
        if (symbol.startsWith("AUDIO_"))
            return root.audio.loaded && root.runAudioHotkey(symbol) // only with an audio box
        if (symbol.startsWith("VIDEO_"))
            return root.runVideoHotkey(symbol)
        if (symbol.startsWith("EDITBOX_"))
            return root.runEditorHotkey(symbol, field)
        if (symbol.startsWith("GRID_"))
            return root.runGridHotkey(symbol)
        // OnMenuSelected1's ids (GLOBAL_OPEN_SUBS..GLOBAL_CHECK_FOR_UPDATES,
        // GLOBAL_SELECT_FROM_VIDEO..GLOBAL_STYLE_MANAGER_CLEAN_STYLE) pass
        // CheckLastKeyEvent (HikariSubFrame.cpp:1005).
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
            // OnMenuOpened: the item is enabled for the formats it converts to.
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
        case "GLOBAL_PLAY_ACTUAL_LINE": // the video panel's "Play the current line"
            if (root.video.hasVideo) {
                root.focusPanel(lineText, Qt.OtherFocusReason) // TM1: maybe in a floating panel
                root.video.playActualLine()
            }
            return true
        case "GLOBAL_STYLE_MANAGER_CLEAN_STYLE": // StyleStore::OnCleanStyles; SY1-clean-toast
            if (editing)
                styleManagerWindow.deleteUnused()
            return true
        case "GLOBAL_JOIN_WITH_PREVIOUS": if (editing) root.app.joinLines("previous"); return true
        case "GLOBAL_JOIN_WITH_NEXT": if (editing) root.app.joinLines("next"); return true
        case "GLOBAL_NEXT_TAB": root.app.changeTab(1); return true
        case "GLOBAL_PREVIOUS_TAB": root.app.changeTab(-1); return true
        case "GLOBAL_REMOVE_LINES": if (editing) root.app.deleteLines(); return true
        // E6: OnMenuSelected's GLOBAL_HIDE_TAGS, OnChangeLine (SubsGrid::NextLine)
        // and OnDelete's GLOBAL_REMOVE_TEXT (HikariSubFrame.cpp:835, 2443-2466).
        case "GLOBAL_HIDE_TAGS": root.app.toggleHideTags(); return true
        case "GLOBAL_PREVIOUS_LINE": if (editing) root.editor.nextLine(-1); return true
        case "GLOBAL_NEXT_LINE": if (editing) root.editor.nextLine(1); return true
        case "GLOBAL_REMOVE_TEXT": if (editing) root.app.deleteText(); return true
        case "GLOBAL_ADD_PAGE": root.app.addPage(); return true
        // V6: SubsGrid::SelVideoLine and HikariSubFrame::OnAudioSnap
        case "GLOBAL_SELECT_FROM_VIDEO": root.app.selectLineFromVideo(); return true
        case "GLOBAL_SNAP_WITH_START": root.app.snapToKeyframe(true); return true
        case "GLOBAL_SNAP_WITH_END": root.app.snapToKeyframe(false); return true
        }
        // Not in the rewrite yet (docs/qt/coverage.md, O2): the key is taken
        // and nothing runs. GLOBAL_SAVE_WITH_VIDEO_NAME changes nothing in
        // legacy either (OnMenuSelected reads the item's check without
        // switching it), nor do the submenu ids (GLOBAL_SORT_LINES,
        // GLOBAL_SORT_SELECTED_LINES, GLOBAL_RECENT_*). GLOBAL_VIDEO_INDEXING
        // is retired (V3-indexing-retired): its bindings are dropped as the
        // hotkeys are read.
        return false
    }
    // EditBox::OnAccelerator for an Editor binding in `field` (the focused
    // text field); Video actions bound for the Editor go to the video.
    // False for an action the rewrite does not have yet.
    function editorHotkey(action, field) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        const tags = { EDITBOX_INSERT_BOLD: "b", EDITBOX_INSERT_ITALIC: "i", EDITBOX_CHANGE_UNDERLINE: "u",
                       EDITBOX_CHANGE_STRIKEOUT: "s" }
        const colours = { EDITBOX_CHANGE_COLOR_PRIMARY: 1, EDITBOX_CHANGE_COLOR_SECONDARY: 2,
                          EDITBOX_CHANGE_COLOR_OUTLINE: 3, EDITBOX_CHANGE_COLOR_SHADOW: 4 }
        // Legacy writes time differences into the edited field (the
        // Translated one in translation mode) whichever field has focus.
        const edited = root.editor.translationMode ? translationText : lineText
        if (tags[action] !== undefined) {
            root.editor.toggleTagIn(field.role, tags[action], field.selectionStart, field.selectionEnd)
            return true
        }
        // TM1: the Original's runs go on the Translated text's selection.
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
        case "EDITBOX_COMMIT_GO_NEXT_LINE": root.app.commitAndAdvance(); return true // V6: NextLine's play-after
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
    // VideoBox::OnAccelerator; Editor and Grid actions bound for the Video
    // window go to theirs.
    function videoHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        if (root.hotkeys.repeatedKey(action, 50)) // VideoBox::OnAccelerator (VideoBox.cpp:1137)
            return true
        // VT2: the tool commands choose their visual tool.
        if (root.visualTools.runHotkey(action))
            return true
        switch (action) {
        case "VIDEO_PLAY_PAUSE": root.video.togglePlay(); return true
        case "VIDEO_5_SECONDS_FORWARD": root.video.seekBy(5000); return true
        case "VIDEO_5_SECONDS_BACKWARD": root.video.seekBy(-5000); return true
        case "VIDEO_MINUTE_FORWARD": root.video.seekBy(60000); return true
        case "VIDEO_MINUTE_BACKWARD": root.video.seekBy(-60000); return true
        case "VIDEO_STOP": root.video.stop(); return true
        // T1: the pointer's position in the video window (VideoBox.cpp:1151).
        case "VIDEO_COPY_COORDS": root.visualTools.copyCoordinatesAtCursor(visualOverlay); return true
        // V3 (VideoBox.cpp:1145-1150, 1170): the folder walk asks first
        // (OnPrew / OnNext), the chapters, Unload video (V3-unload-video).
        case "VIDEO_PREVIOUS_FILE": videoFileQuestion.ask(false); return true
        case "VIDEO_NEXT_FILE": videoFileQuestion.ask(true); return true
        case "VIDEO_PREVIOUS_CHAPTER": root.video.previousChapter(); return true
        case "VIDEO_NEXT_CHAPTER": root.video.nextChapter(); return true
        case "VIDEO_DELETE_FILE": root.video.unloadVideo(); return true
        // V4 (VideoBox.cpp:1147-1173).
        case "VIDEO_VOLUME_PLUS": root.videoView.stepVolume(true); return true
        case "VIDEO_VOLUME_MINUS": root.videoView.stepVolume(false); return true
        case "VIDEO_HIDE_PROGRESS_BAR": root.videoView.toggleProgressBar(); return true
        // V5 (VideoBox.cpp:1161-1162): SetFullscreen().
        case "VIDEO_FULL_SCREEN": root.videoFullscreen.toggle(0); return true
        // FS1-bar-mode: Auto, Always, Never.
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
    // SubsGrid::OnAccelerator; Editor and Video actions bound for the Grid
    // go to theirs.
    function gridHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        // SubsGrid::OnAccelerator hands audio ids to the audio box (when
        // there is one; the key is the Grid's either way), whose
        // OnAccelerator checks CheckLastKeyEvent (SubsGrid.cpp:810-819,
        // AudioBox.cpp:670-674).
        if (action.startsWith("AUDIO_"))
            return !root.hotkeys.repeatedKey(action) && root.audio.runHotkey(action)
        if (root.hotkeys.repeatedKey(action))
            return true
        // GRID_HIDE_*: the column's bit (id - 4000; CPS 512, wraps 8192).
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
        case "GRID_SELECT_VISIBLE_LINES": root.app.selectLinesVisibleOnVideo(); return true // V6
        case "GRID_SHOW_PREVIEW": if (root.app.canShowPreview()) root.app.showPreview(); return true // R2
        case "GRID_HIDE_SELECTED": root.app.hideSelectedLines(); return true
        case "GRID_FILTER": root.app.filterLines(); return true
        case "GRID_FILTER_BY_NOTHING": root.app.turnOffFiltering(); return true
        // SubsGrid::OnAccelerator (SubsGrid.cpp:879-881): only for a ".mkv" or ".ogm" video.
        case "GRID_SUBS_FROM_MKV": matroskaSubtitles.begin(); return true
        case "GRID_FILTER_INVERT": root.gridFilter.inverted = !root.gridFilter.inverted; return true
        case "GRID_FILTER_DO_NOT_RESET": root.gridFilter.addToFilter = !root.gridFilter.addToFilter; return true
        case "GRID_FILTER_AFTER_SUBS_LOAD": root.gridFilter.afterLoad = !root.gridFilter.afterLoad; return true
        // The hotkey toggles from the stored option, not from the Grid's own
        // "ignore filtering" (which "Set default" leaves as it was).
        case "GRID_FILTER_IGNORE_IN_ACTIONS":
            root.gridFilter.ignoreInActions = root.hotkeys.ignoreFilteringFromOption()
            return true
        // KB3: these ran only from the Grid menu, so their keys did nothing
        // (SubsGrid::OnAccelerator, SubsGrid.cpp:839-881, 938).
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
    // AudioBox::OnAccelerator; Video, Editor and Grid actions bound for the
    // Audio window go to theirs (AudioBox::SetAccels).
    function audioHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        if (action.startsWith("AUDIO_"))
            return !root.hotkeys.repeatedKey(action) && root.audio.runHotkey(action) // AudioBox.cpp:673
        if (action.startsWith("VIDEO_"))
            return root.runVideoHotkey(action)
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, root.editedField())
        return action.startsWith("GRID_") && root.runGridHotkey(action)
    }
    // PK1's hotkey capture: Shift+click on a command opens it in its
    // compact window.
    HotkeyCapture {
        id: hotkeyMapping
        objectName: "hotkeyMapping"
        hotkeys: root.hotkeys
        settings: root.app.settings
    }
    // S2: committed automation shortcuts, application-wide.
    Repeater {
        model: root.automationHotkeys.shortcuts
        delegate: Item {
            required property var modelData
            Shortcut {
                sequence: modelData.keys
                context: Qt.ApplicationShortcut
                // The scripts' entries follow the static ones in the frame's
                // table (map order: ids from 30100), so a static Global
                // binding of the same keys wins, as the Video window's
                // Play / Pause does (KB2: it acts from every panel).
                // Off under a modal dialog, which keeps the panel's run
                // controls out of reach.
                enabled: !root.menuHasKeys && !root.workspaceLayout.modalOpen
                         && !root.hotkeys.macroTakenSequences.includes(modelData.keys)
                onActivated: root.automationHotkeys.run(modelData.legacyName)
            }
        }
    }
}
