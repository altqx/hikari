import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    required property AutomationDialogController automationDialogs
    required property AutomationFilePickerController automationPicker
    required property LogController log
    required property TagButtonsController tagButtons
    required property ColourPickerController colourPicker
    required property WorkspaceLayoutController workspaceLayout
    required property ShiftTimesController shiftTimes
    required property GridFilterController gridFilter
    required property var automationHotkeys
    required property var updates
    required property var styleManager
    required property var fontCollector // Y8: FontCollectorController
    required property var fontCatalogs // Y6: FontCatalogsController
    required property var matroska // Y9: MatroskaController (GRID_SUBS_FROM_MKV)
    required property var hotkeys // O2: the shortcut editor (HotkeysController)
    required property var settingsImport // O3: SettingsImportController
    required property VisualToolsController visualTools // T1: the Video panel's visual tools

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
    readonly property var conversionItems: [
        ["ass", qsTr("Convert to ASS"), "GLOBAL_CONVERT_TO_ASS"], ["srt", qsTr("Convert to SRT"), "GLOBAL_CONVERT_TO_SRT"],
        ["mdvd", qsTr("Convert to MDVD"), "GLOBAL_CONVERT_TO_MDVD"], ["mpl2", qsTr("Convert to MPL2"), "GLOBAL_CONVERT_TO_MPL2"],
        ["tmp", qsTr("Convert to TMP"), "GLOBAL_CONVERT_TO_TMP"]]

    // With their GLOBAL_SORT_ALL_BY_* / GLOBAL_SORT_SELECTED_BY_* ids.
    readonly property var sortKeys: [
        { key: "start", label: qsTr("The starting time"), all: "GLOBAL_SORT_ALL_BY_START_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_START_TIMES" },
        { key: "end", label: qsTr("End time"), all: "GLOBAL_SORT_ALL_BY_END_TIMES",
          selected: "GLOBAL_SORT_SELECTED_BY_END_TIMES" },
        { key: "style", label: qsTr("Styles"), all: "GLOBAL_SORT_ALL_BY_STYLE", selected: "GLOBAL_SORT_SELECTED_BY_STYLE" },
        { key: "actor", label: qsTr("Actor"), all: "GLOBAL_SORT_ALL_BY_ACTOR", selected: "GLOBAL_SORT_SELECTED_BY_ACTOR" },
        { key: "effect", label: qsTr("Effect"), all: "GLOBAL_SORT_ALL_BY_EFFECT", selected: "GLOBAL_SORT_SELECTED_BY_EFFECT" },
        { key: "layer", label: qsTr("Layer"), all: "GLOBAL_SORT_ALL_BY_LAYER", selected: "GLOBAL_SORT_SELECTED_BY_LAYER" }
    ]

    // E2: a custom tag button. Types 0 and 1 work in the focused field like
    // Bold; plain text (type 2) goes, as in legacy, into the main field (the
    // translation in translation mode, unless it is empty).
    function applyTagButton(index) {
        // EditBox::OnButtonTag reads the button's option as stored now.
        const b = root.tagButtons.pressed(index)
        if (b.tag === undefined || b.tag.length === 0)
            return false
        let field = translationText.activeFocus ? translationText : lineText
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
        if (!result.ok)
            return // the log window shows the problem
        else if (result.rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(result.rows)
    }
    // Legacy ReloadSubsIfModified when the window becomes active.
    function checkExternalChange() {
        if (root.app.externalChange() === "modified")
            reloadPrompt.show()
    }
    onActiveChanged: {
        if (active)
            checkExternalChange()
    }
    onClosing: close => {
        root.workspaceLayout.save()
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
    Connections {
        target: root.app
        function onCloseFinished(done, problem) {
            if (done) {
                closeReview.close()
                if (root.app.quitApproved)
                    root.close()
            } else {
                closeReview.problem = problem
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
        // button rows appear then), so the body is what gets 170 px.
        const deficit = Math.round(170 - audioPanel.bodyHeight)
        if (deficit !== 0)
            Docking.resizeInLayout("Audio", 0, 0, 0, deficit)
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
        dockingArea.addDockWidget(referenceDock, KDDW.KDDockWidgets.Location_OnBottom, gridDock, Qt.size(0, 160))
        if (!root.shell.hasReference)
            referenceDock.close()
        // Tools start closed (docs/qt/ux/workspaces.md); Timing tabs with the editor.
        editorDock.addDockWidgetAsTab(timingDock)
        editorDock.setAsCurrentTab()
        timingDock.close()
    }
    Connections {
        target: root.shell
        function onHasReferenceChanged() {
            if (root.shell.hasReference)
                referenceDock.open()
            else
                referenceDock.close()
        }
    }

    // Major panels in F6 order; a hidden panel is skipped.
    readonly property list<Item> panels: [videoPanel, audioPanel, editorPanel, gridPanel, referencePanel, timingPanel,
                                          searchPanel]
    // D1: their docks, in the same order.
    readonly property var dockList: [videoDock, audioDock, editorDock, gridDock, referenceDock, timingDock, searchDock]

    // A preset is the Editing arrangement with the panels it leaves closed
    // (Timing: Video; Translation and Typesetting: Audio).
    function applyPreset(name) {
        root.workspaceLayout.resetLayout()
        if (name === "Timing")
            videoDock.close()
        else if (name === "Translation" || name === "Typesetting")
            audioDock.close()
        root.workspaceLayout.preset = name
        root.workspaceLayout.save()
    }

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

    // GLOBAL_SHIFT_TIMES: shifting only start or end times asks first (legacy).
    function runShiftTimes() {
        const which = root.shiftTimes.settings.whichTimes
        const pp = root.shiftTimes.settings.postprocessor
        if (which !== 0 && !(pp >= 16 && (pp & 15) !== 0)) {
            shiftConfirm.which = which
            shiftConfirm.open()
            return
        }
        shiftMessage.text = root.app.shiftTimes()
    }

    // The dock of the panel with the focus (the Grid's otherwise).
    function focusedDock() {
        const w = root.workspaceLayout.focusWindow
        const i = panels.findIndex(p => p.activeFocus && p.Window.window === w)
        return dockList[i >= 0 ? i : 3]
    }

    // D1 keyboard placement: kind 0 tab with, 1 left of, 2 above, 3 right of,
    // 4 below, 5 float. The moved panel keeps the focus.
    function placePanel(dock, kind, target) {
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

    // View > Panels > Show: open the panel, bring its window up and focus it.
    function showPanel(dock) {
        dock.open()
        root.workspaceLayout.keepFloatingPanelsOnScreen()
        dock.raise()
        root.focusPanel(panels[dockList.indexOf(dock)], Qt.OtherFocusReason)
    }

    function panelOf(item) {
        for (let p = item; p; p = p.parent)
            for (const panel of panels)
                if (p === panel)
                    return panel
        return null
    }

    function cyclePanels(step) {
        const shown = panels.filter(p => p.visible)
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
        const next = current < 0 ? (step > 0 ? 0 : shown.length - 1)
                                 : (current + step + shown.length) % shown.length
        root.focusPanel(shown[next], Qt.TabFocusReason)
    }

    // The panel waiting for its window's activation (focusPanel).
    property var pendingFocus: null
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
            pending.panel.forceActiveFocus(pending.reason)
            // Again after the other activation handlers (the docking layer
            // focuses its own frame when a floating window activates).
            Qt.callLater(() => pending.panel.forceActiveFocus(pending.reason))
        }
    }

    // Focus a panel, activating its window first when that is not the focus
    // window (a floating panel, or the main window from one). Activation is
    // asynchronous on X11 and Wayland, and the window gives the focus to its
    // own item when it activates, so the panel takes it again then.
    function focusPanel(panel, reason) {
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
    readonly property bool shellActive: (root.workspaceLayout.focusWindow === root || floatingPanelActive) && !menuHasKeys
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
    // Global binding of the same keys comes first.
    Shortcut {
        sequences: ["F6"]
        context: Qt.ApplicationShortcut
        enabled: !root.menuHasKeys && !root.hotkeys.globalSequences.includes("F6")
        onActivated: root.cyclePanels(1)
    }
    Shortcut {
        sequences: ["Shift+F6"]
        context: Qt.ApplicationShortcut
        enabled: !root.menuHasKeys && !root.hotkeys.globalSequences.includes("Shift+F6")
        onActivated: root.cyclePanels(-1)
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
    }
    Connections {
        target: root.app
        // P6: the shown tab's Grid scroll (legacy each tab kept its Grid), and
        // legacy ChangePage's ReloadSubsIfModified.
        // The application ignores the Grid's scroll reports from the tab
        // change until scrollRestored(), so the model reset's transient 0
        // never replaces the tab's own scroll.
        function onTabShown(scroll) {
            Qt.callLater(() => {
                grid.contentY = scroll * grid.rowHeight
                root.app.scrollRestored()
                root.checkExternalChange()
            })
        }
    }

    // Classic menus. Commands join through the shared action system as their
    // cards land. K1: the items legacy drew with a bitmap (HikariSubFrame.cpp's
    // AppendTool) show the set's icon (ShellMenuItem, ShellMenu).
    menuBar: MenuBar {
        MenuBarItem {
            objectName: "fileMenuBarItem"
            menu: ShellMenu {
                title: qsTr("&File")
                ShellMenuItem {
                    iconRole: "open-subtitles"
                    action: Action {
                        id: openAction
                        text: qsTr("&Open…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS")) openDialog.open()
                    }
                }
                ShellMenu {
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
                            text: modelData.label
                            onTriggered: root.openSubtitles(modelData.path)
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
                    objectName: "closeMenuItem"
                    action: Action {
                        id: closeAction
                        text: qsTr("&Close")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_CLOSE_PAGE")) root.beginClose("close")
                    }
                }
                ShellMenuItem {
                    iconRole: "open-video"
                    objectName: "openVideoMenuItem"
                    action: Action {
                        id: openVideoAction
                        text: qsTr("Open &Video…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.open()
                    }
                }
                ShellMenuItem {
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
                    iconRole: "save-all"
                    objectName: "saveAllMenuItem"
                    action: Action {
                        id: saveAllAction
                        text: qsTr("Save &all")
                        enabled: root.shell.hasEditingTarget
                        onTriggered: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_ALL_SUBS"))
                                return
                            if (root.app.saveAll())
                                root.openSaveDialog()
                        }
                    }
                }
                ShellMenuItem {
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
                    objectName: "openAutoSaveMenuItem"
                    action: Action {
                        id: openAutoSaveAction
                        text: qsTr("Open auto save")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_AUTO_SAVE")) recoveryWindow.showBundles()
                    }
                }
                ShellMenuItem {
                    objectName: "removeTemporaryMenuItem"
                    action: Action {
                        id: removeTemporaryAction
                        text: qsTr("Remove temporary files")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_DELETE_TEMPORARY_FILES")) temporaryFilesWindow.showFiles()
                    }
                }
                ShellMenuItem {
                    objectName: "logMenuItem"
                    action: Action {
                        text: qsTr("Show / Hide log window")
                        onTriggered: root.log.toggleWindow()
                    }
                }
                // P6: legacy "Last session" submenu.
                ShellMenu {
                    iconRole: "last-session"
                    objectName: "lastSessionMenu"
                    title: qsTr("Last session")
                    ShellMenuItem {
                        id: loadLastSessionItem
                        iconRole: "last-session"
                        objectName: "loadLastSessionMenuItem"
                        text: qsTr("Load last session")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_LAST_SESSION")) sessionWindows.load()
                    }
                    ShellMenuItem {
                        id: loadSessionFileItem
                        objectName: "loadSessionFileMenuItem"
                        text: qsTr("Load session from file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_EXTERNAL_SESSION")) sessionWindows.chooseSessionToLoad()
                    }
                    ShellMenuItem {
                        id: saveSessionFileItem
                        objectName: "saveSessionFileMenuItem"
                        text: qsTr("Save session to file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_EXTERNAL_SESSION")) sessionWindows.chooseSessionToSave()
                    }
                    ShellMenuItem {
                        objectName: "askForLastSessionMenuItem"
                        text: qsTr("Ask whether to load the last session at program startup")
                        checkable: true
                        checked: root.app.sessionRestore === 1
                        onToggled: root.app.sessionRestore = checked ? 1 : 0
                    }
                    ShellMenuItem {
                        objectName: "loadLastSessionOnStartMenuItem"
                        text: qsTr("Load last session after program start")
                        checkable: true
                        checked: root.app.sessionRestore === 2
                        onToggled: root.app.sessionRestore = checked ? 2 : 0
                    }
                }
                // O1: legacy GLOBAL_SETTINGS, the Options dialog.
                ShellMenuItem {
                    iconRole: "settings"
                    objectName: "settingsMenuItem"
                    action: Action {
                        id: settingsAction
                        text: qsTr("&Settings")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SETTINGS")) settingsDialog.openDialog()
                    }
                }
                // O3: the one-shot legacy settings importer (no legacy item).
                ShellMenuItem {
                    objectName: "importSettingsMenuItem"
                    text: qsTr("Import legacy settings...")
                    enabled: root.settingsImport.available
                    onTriggered: settingsImportDialog.openDialog()
                }
                ShellMenuItem {
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
                iconRole: "undo"
                action: Action {
                    id: undoAction
                    text: qsTr("&Undo")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_UNDO")) root.editor.undo()
                }
            }
            ShellMenuItem {
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
                        text: modelData.label
                        onTriggered: if (!root.hotkeyGesture(modelData.all)) root.app.sortLines(modelData.key, false)
                    }
                    onObjectAdded: (index, object) => sortAllMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortAllMenu.removeItem(object)
                }
            }
            ShellMenu {
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
                        text: modelData.label
                        onTriggered: if (!root.hotkeyGesture(modelData.selected)) root.app.sortLines(modelData.key, true)
                    }
                    onObjectAdded: (index, object) => sortSelectedMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortSelectedMenu.removeItem(object)
                }
            }
            ShellMenuItem {
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
                iconRole: "history"
                objectName: "historyMenuItem"
                action: Action {
                    id: historyAction
                    text: qsTr("&History")
                    enabled: root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HISTORY")) historyWindow.show()
                }
            }
            ShellMenuItem {
                iconRole: "select-lines"
                objectName: "misspellMenuItem"
                action: Action {
                    id: misspellAction
                    text: qsTr("Fix minor errors (experimental)")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_MISSPELLS_REPLACER")) misspellDialog.toggle()
                }
            }
            ShellMenuItem {
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
            Action {
                id: automationHotkeysAction
                text: qsTr("Open shortcut mapping window")
                onTriggered: {
                    if (root.hotkeyGesture("GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW"))
                        return
                    root.automationHotkeys.begin()
                    automationHotkeysWindow.show()
                }
            }
            ShellMenuItem {
                iconRole: "automation"
                objectName: "loadScriptMenuItem"
                action: Action {
                    id: loadScriptAction
                    text: qsTr("&Load script…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_SCRIPT")) scriptDialog.open()
                }
            }
            ShellMenuItem {
                iconRole: "automation"
                objectName: "reloadAutoloadMenuItem"
                action: Action {
                    id: reloadAutoloadAction
                    text: qsTr("Refresh autoload scripts")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_RELOAD_AUTOLOAD")) root.automation.reloadAutoload()
                }
            }
            ShellMenuItem {
                objectName: "loadLastScriptMenuItem"
                action: Action {
                    id: loadLastScriptAction
                    text: qsTr("Run the last loaded script")
                    // Legacy's modal progress dialog blocks it while a macro runs.
                    enabled: !root.automation.running
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT")) root.automation.runLastLoadedScript()
                }
            }
            ShellMenuItem {
                objectName: "rerunMenuItem"
                action: Action {
                    text: qsTr("Rerun last macro")
                    enabled: root.automation.canRerun
                    onTriggered: root.automation.rerunLast()
                }
            }
            ShellMenuItem {
                objectName: "automationManagerMenuItem"
                action: Action {
                    text: qsTr("Automation &manager")
                    onTriggered: automationManagerWindow.show()
                }
            }
            MenuSeparator {}
            Instantiator {
                model: root.macroItems
                delegate: ShellMenuItem {
                    required property var modelData
                    objectName: "macro_" + modelData.name
                    text: modelData.name
                    enabled: !root.automation.running
                    // Automation.cpp:1376-1379: Shift maps the macro's hotkey
                    // (OnMapHkey(-1, "Script <file>-<n>"), no window choice).
                    onTriggered: {
                        const name = root.automationHotkeys.legacyNameFor(modelData.path, modelData.ordinal)
                        if (!root.hotkeyGesture(name, 0, "macro"))
                            root.automationManager.run(modelData.path, modelData.ordinal)
                    }
                }
                onObjectAdded: (index, object) => automationMenu.insertItem(6 + index, object)
                onObjectRemoved: (index, object) => automationMenu.removeItem(object)
            }
        }
        ShellMenu {
            objectName: "videoMenu"
            title: qsTr("&Video")
            // Each item: Shift+click maps its Global hotkey (OnMenuSelected).
            ShellMenuItem {
                iconRole: "open-video"
                action: Action {
                    text: qsTr("Open &video…")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.open()
                }
            }
            ShellMenuItem {
                iconRole: "frame-previous"
                action: Action {
                    id: previousFrameAction
                    text: qsTr("Previous frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PREVIOUS_FRAME")) root.video.stepFrames(-1)
                }
            }
            ShellMenuItem {
                iconRole: "frame-next"
                action: Action {
                    id: nextFrameAction
                    text: qsTr("Next frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_NEXT_FRAME")) root.video.stepFrames(1)
                }
            }
            ShellMenuItem {
                iconRole: "video-to-start-time"
                action: Action {
                    id: goToStartAction
                    text: qsTr("Go to start time"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_START_TIME")) root.video.goToLineStart()
                }
            }
            ShellMenuItem {
                iconRole: "video-to-end-time"
                action: Action {
                    id: goToEndAction
                    text: qsTr("Go to end time of line"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_END_TIME")) root.video.goToLineEnd()
                }
            }
            ShellMenuItem {
                iconRole: root.video.playing ? "media-pause" : "media-play"
                action: Action {
                    id: playPauseAction
                    text: qsTr("Play / Pause"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PLAY_PAUSE")) root.video.togglePlay()
                }
            }
            ShellMenuItem {
                iconRole: "keyframe-previous"
                action: Action {
                    id: previousKeyframeAction
                    text: qsTr("Go to previous keyframe"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_PREVIOUS_KEYFRAME")) root.video.previousKeyframe()
                }
            }
            ShellMenuItem {
                iconRole: "keyframe-next"
                action: Action {
                    id: nextKeyframeAction
                    text: qsTr("Go to next keyframe"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_NEXT_KEYFRAME")) root.video.nextKeyframe()
                }
            }
            ShellMenuItem {
                iconRole: "open-keyframes"
                action: Action {
                    id: openKeyframesAction
                    text: qsTr("Open keyframes")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_KEYFRAMES")) keyframesDialog.open()
                }
            }
            // A3: GLOBAL_SET_AUDIO_FROM_VIDEO, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO
            // (legacy OnMenuOpened: ABox != nullptr && editor; the rewrite has
            // no GLOBAL_EDITOR switch, and its editor is the editing target's).
            ShellMenuItem {
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
        // A1: legacy Audio menu (GLOBAL_OPEN_AUDIO, GLOBAL_RECENT_AUDIO,
        // GLOBAL_AUDIO_FROM_VIDEO, GLOBAL_OPEN_DUMMY_AUDIO, GLOBAL_CLOSE_AUDIO).
        ShellMenu {
            id: audioMenu
            objectName: "audioMenu"
            title: qsTr("A&udio")
            ShellMenuItem {
                iconRole: "open-audio"
                objectName: "openAudioMenuItem"
                action: Action {
                    id: openAudioAction
                    text: qsTr("Open audio")
                    onTriggered: {
                        if (root.hotkeyGesture("GLOBAL_OPEN_AUDIO"))
                            return
                        audioDialog.currentFolder = root.app.audioDialogFolder()
                        audioDialog.open()
                    }
                }
            }
            ShellMenu {
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
                        text: modelData.label
                        onTriggered: root.audio.openAudio(modelData.path)
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
                objectName: "dummyAudioMenuItem"
                action: Action {
                    id: dummyAudioAction
                    text: qsTr("Open blank 2h30m audio")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_DUMMY_AUDIO")) root.audio.openDummy()
                }
            }
            ShellMenuItem {
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
            title: qsTr("Vie&w") // legacy has no View menu; Alt+V stays with &Video
            // D1: each panel can be shown (and focused), hidden, floated or
            // docked; Reset layout returns to the Editing arrangement.
            ShellMenu {
                id: panelsMenu
                objectName: "panelsMenu"
                title: qsTr("&Panels")
                Instantiator {
                    model: root.dockList
                    delegate: ShellMenu {
                        required property var modelData
                        objectName: "panelMenu" + modelData.uniqueName
                        title: modelData.title
                        ShellMenuItem {
                            objectName: "panelShow" + modelData.uniqueName
                            text: qsTr("Show")
                            onTriggered: root.showPanel(modelData)
                        }
                        ShellMenuItem {
                            objectName: "panelHide" + modelData.uniqueName
                            text: qsTr("Hide")
                            enabled: modelData.isOpen
                            onTriggered: modelData.close()
                        }
                        ShellMenuItem {
                            objectName: "panelFloat" + modelData.uniqueName
                            text: qsTr("Float")
                            enabled: modelData.isOpen && !modelData.isFloating
                            onTriggered: root.setPanelFloating(modelData, true)
                        }
                        ShellMenuItem {
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
                objectName: "movePanel"
                text: qsTr("&Move panel…")
                onTriggered: placementWindow.openFor(root.focusedDock())
            }
            // Built-in starting arrangements (docs/qt/ux/workspaces.md); the
            // tools they open come with the tool cards.
            ShellMenu {
                id: presetMenu
                objectName: "layoutPresetMenu"
                title: qsTr("Layout &preset")
                Instantiator {
                    model: [
                        { name: "Editing", label: qsTr("Editing") },
                        { name: "Timing", label: qsTr("Timing") },
                        { name: "Translation", label: qsTr("Translation") },
                        { name: "Typesetting", label: qsTr("Typesetting") }
                    ]
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "preset" + modelData.name
                        text: modelData.label
                        checkable: true
                        checked: root.workspaceLayout.preset === modelData.name
                        onTriggered: root.applyPreset(modelData.name)
                    }
                    onObjectAdded: (index, object) => presetMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => presetMenu.removeItem(object)
                }
            }
            ShellMenuItem {
                objectName: "resetLayout"
                text: qsTr("&Reset layout")
                onTriggered: root.applyPreset(root.workspaceLayout.preset)
            }
            ShellMenuItem {
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
                id: showShiftTimesItem
                iconRole: "shift-times"
                objectName: "showShiftTimes"
                text: qsTr("Shift &times...")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_SHIFT_TIMES")) root.showPanel(timingDock)
            }
            ShellMenuItem {
                id: runShiftTimesItem
                objectName: "runShiftTimes"
                text: qsTr("Shift times / run time post processor")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_TIMES")) root.runShiftTimes()
            }
            ShellMenuItem {
                id: styleManagerItem
                iconRole: "styles"
                objectName: "styleManagerMenuItem"
                text: qsTr("Style &manager")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_STYLE_MANAGER")) styleManagerWindow.showFor(root.app.activeLineStyle())
            }
            ShellMenuItem {
                id: assPropertiesItem
                iconRole: "script-properties"
                objectName: "assProperties"
                text: qsTr("ASS file properties")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_ASS_PROPERTIES")) scriptPropertiesDialog.openFor()
            }
            ShellMenu {
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
                        iconRole: ["convert-ass", "convert-srt", "convert-mdvd", "convert-mpl2", "convert-tmp"][index]
                        text: modelData[1]
                        enabled: conversionMenu.targets.indexOf(modelData[0]) >= 0
                        onTriggered: if (!root.hotkeyGesture(modelData[2])) conversionDialog.openFor(modelData[0], modelData[1])
                    }
                }
            }
            // Legacy SubsMenu: Font collector after Conversion and Shift times
            // (HikariSubFrame.cpp:338), enabled for subsFormat < SRT (2407).
            ShellMenuItem {
                id: fontCollectorItem
                objectName: "fontCollectorMenuItem"
                iconRole: "font-collector"
                text: qsTr("Font collector")
                enabled: root.shell.hasEditingTarget && root.shell.assColumns
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_FONT_COLLECTOR")) fontCollectorDialog.showOnce()
            }
            ShellMenuItem {
                id: resampleItem
                iconRole: "resample"
                objectName: "resampleMenuItem"
                text: qsTr("Resample subtitles")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS_RESAMPLE")) resampleDialog.openDialog()
            }
            // Legacy HikariSubFrame: after Resample subtitles.
            ShellMenuItem {
                id: checkSpellingItem
                iconRole: "spellchecker"
                objectName: "checkSpellingMenuItem"
                text: qsTr("Check spelling")
                enabled: root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SPELLCHECKER")) spellCheckerDialog.openDialog()
            }
        }
        ShellMenu {
            title: qsTr("&Help")
            ShellMenuItem {
                iconRole: "help"
                action: Action {
                    id: websiteAction
                    text: qsTr("HikariSub &website")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HELP")) Qt.openUrlExternally("https://altqx.com")
                }
            }
            ShellMenuItem {
                iconRole: "report-issue"
                objectName: "reportIssueMenuItem"
                action: Action {
                    id: reportIssueAction
                    text: qsTr("&Report an issue")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ANSI")) root.app.reportIssue()
                }
            }
            ShellMenuItem {
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
                iconRole: "about"
                objectName: "aboutMenuItem"
                action: Action {
                    id: aboutAction
                    text: qsTr("&About")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_ABOUT")) aboutDialog.open()
                }
            }
            ShellMenuItem {
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
        readOnly: !root.editor.editable
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true // the Original's selection survives for "Paste the selected"
        Layout.fillWidth: true
        Layout.fillHeight: true

        property bool syncing: false
        function sync() {
            if (text === shown)
                return
            syncing = true
            const caret = cursorPosition
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
        SpellingHighlighter {
            id: spellMarks
            objectName: field.objectName + "SpellMarks"
            document: field.textDocument
            // EDITOR_SPELLCHECKER: the theme's (K2; legacy's dark and light defaults).
            colour: Theme.spellcheck
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
            ShellMenuItem { text: qsTr("&Copy"); enabled: field.selectedText.length > 0; onTriggered: field.copy() }
            ShellMenuItem { text: qsTr("Cu&t"); enabled: field.selectedText.length > 0 && !field.readOnly; onTriggered: field.cut() }
            ShellMenuItem { text: qsTr("&Paste"); enabled: !field.readOnly; onTriggered: field.paste() }
            MenuSeparator {}
            ShellMenuItem {
                id: spellingOnItem
                objectName: field.objectName + "SpellingOn"
                text: qsTr("Spellchecker")
                checkable: true
                checked: root.app.spellingOn
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
                    objectName: field.objectName + "Languages"
                    title: qsTr("Installed languages")
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
                objectName: field.objectName + "AddWord"
                text: qsTr("&Add word \"%1\" to dictionary").arg(field.misspell.word || "")
                visible: !!field.misspell.word
                height: visible ? implicitHeight : 0
                onTriggered: {
                    if (!root.app.addEditorWord(field.misspell.word)) {
                        spellingNotice.text = qsTr("Error. Word \"%1\" was not added.").arg(field.misspell.word)
                        spellingNotice.open()
                    }
                }
            }
            ShellMenuItem {
                text: qsTr("&Delete")
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
                    fixSuggestions.openFor(field.role, position, found.suggestions)
                }
            }
        }
        // Legacy EDITBOX bindings (O2, HotkeysController::actionFor): the
        // text field's own plain Enter (commit and go to the next line) and
        // TabPanel's fixed numpad Enter and Ctrl+numpad Enter first, then the
        // Editor's bindings, which act before the application's shortcuts;
        // composition keeps its own Enter. Escape discards the draft and
        // Ctrl+Z / Ctrl+Y undo and redo it (accepted transaction policy).
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
        Keys.onShortcutOverride: event => event.accepted = hotkeyAction(event) !== ""
        Keys.onPressed: event => {
            const ctrl = event.modifiers & Qt.ControlModifier
            const action = hotkeyAction(event)
            if (action !== "") {
                root.runEditorHotkey(action, field) // an action not here yet still takes the key
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
    }

    // A panel's body. Its dock's title bar (DockTitleBar.qml) is its only
    // visible header: the user dropped the in-panel title row on 2026-10-05,
    // since with docking it repeated the dock's title. The title stays the
    // panel's name for assistive technology.
    component Panel: FocusScope {
        id: panel
        property string title
        // The panel's name for assistive technology: its title unless the
        // title names something else (the Grid's names the editing target).
        property string accessibleName: title
        default property alias content: body.data
        readonly property real bodyHeight: body.height
        activeFocusOnTab: false
        Accessible.role: Accessible.Pane
        Accessible.name: accessibleName
        Accessible.description: accessibleName !== title ? title : ""

        // K2 (visual-language.md, "Keyboard focus"): the boundary stays
        // `line`; the panel holding the focus is ringed on its dock header
        // (DockTitleBar.qml) in the focus role, not bordered in the accent.
        Rectangle {
            anchors.fill: parent
            color: panel.palette.base
            border.color: panel.palette.mid
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
            const subtitles = root.app.openDropped(drop.urls)
            if (subtitles.length > 0)
                root.openSubtitles(subtitles)
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

    // D1: the Classic panels dock, float, tab and close (KDDockWidgets behind
    // ui/docking.h; docs/qt/docking.md). Closing a panel hides its view; its
    // controller and any draft stay.
    KDDW.DockingArea {
        id: dockingArea
        objectName: "dockingArea"
        anchors.fill: parent
        uniqueName: "Classic"

        KDDW.DockWidget {
            id: videoDock
            objectName: "videoDock"
            uniqueName: "Video"
            title: qsTr("Video")
            Panel {
                id: videoPanel
                anchors.fill: parent
                objectName: "videoPanel"
                title: qsTr("Video")
                // Frame stepping is the Global bindings GLOBAL_PREVIOUS_FRAME /
                // GLOBAL_NEXT_FRAME (Left / Right by default), from the shell's
                // table (legacy VideoBox has no arrow keys of its own).
                // O2: the Video window's bindings (VideoBox's table; by default
                // VIDEO_PLAY_PAUSE Space, VIDEO_5_SECONDS_* L / ;,
                // VIDEO_MINUTE_* Up / Down), before the application's shortcuts.
                // T1: Esc cancels an open visual gesture first; keys no binding
                // takes go to the visual tool (a nudge), as VideoBox::OnKeyPress
                // hands them to the Visuals.
                Keys.onShortcutOverride: event => event.accepted = (event.key === Qt.Key_Escape && root.visualTools.gestureActive)
                                                  || root.hotkeys.actionFor(3, event.key, event.modifiers) !== ""
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape && root.visualTools.escape()) {
                        event.accepted = true
                        return
                    }
                    const action = root.hotkeys.actionFor(3, event.key, event.modifiers)
                    if (action !== "") {
                        root.runVideoHotkey(action)
                        event.accepted = true
                    } else if (root.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                        event.accepted = true
                    }
                }
                Keys.onReleased: event => event.accepted = root.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

                // The legacy "Associated files" confirmation, inline: the
                // Document stays editable whatever is chosen.
                Frame {
                    id: associationOffer
                    objectName: "associationOffer"
                    visible: root.video.offering
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    z: 1
                    RowLayout {
                        anchors.fill: parent
                        Label {
                            objectName: "associationText"
                            text: root.video.offer
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Button {
                            objectName: "loadAssociated"
                            text: qsTr("Load associated")
                            onClicked: root.video.loadAssociated()
                        }
                        Button {
                            objectName: "dismissAssociation"
                            text: qsTr("No")
                            onClicked: root.video.dismissOffer()
                        }
                    }
                }
                // T1: the tool rail beside the canvas (layout A).
                VisualToolRail {
                    id: visualRail
                    tools: root.visualTools
                    anchors { left: parent.left; top: parent.top; bottom: videoControls.top }
                }
                VideoPresenter {
                    id: presenter
                    objectName: "videoPresenter"
                    visible: root.video.hasVideo
                    anchors { left: visualRail.right; right: parent.right; top: parent.top; bottom: videoControls.top }
                    // The visual tools' shared view places the frame (legacy UpdateRects).
                    videoRect: root.visualTools.videoRect
                    sourceRect: root.visualTools.sourceRect
                    Component.onCompleted: root.video.attachPresenter(presenter)
                }
                VisualOverlay {
                    id: visualOverlay
                    anchors.fill: presenter
                    tools: root.visualTools
                    focusTarget: videoPanel
                    panelHeight: videoControls.height
                }
                Label {
                    anchors.centerIn: presenter
                    visible: !root.video.hasVideo
                    text: root.video.status
                }
                // The seek bar and the legacy times field (keyframes highlighted).
                ColumnLayout {
                    id: videoControls
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    spacing: 2
                VisualToolValues {
                    Layout.fillWidth: true
                    // Its fields and buttons do not shrink: wider text (a
                    // translation, a larger font) or a narrow panel must not
                    // widen the column and push Next frame out of the panel.
                    Layout.minimumWidth: 0
                    clip: true
                    tools: root.visualTools
                }
                Slider {
                    objectName: "videoSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0, root.video.frameCount - 1)
                    stepSize: 1
                    value: Math.max(0, root.video.frame)
                    enabled: root.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Video position")
                    onMoved: root.video.showFrameAt(Math.round(value))
                }
                Label {
                    objectName: "videoTimes"
                    Layout.fillWidth: true
                    text: root.video.times
                    color: root.video.keyframeShown ? Theme.warning : palette.windowText
                    Accessible.name: qsTr("Video times")
                }
                RowLayout {
                    Layout.fillWidth: true
                    // Legacy VideoBox's bitmap buttons (VIDEO_PLAY_PAUSE,
                    // GLOBAL_PLAY_ACTUAL_LINE, VIDEO_STOP): the binding in the
                    // tooltip, and Shift+click maps it (BitmapButton). K1: the
                    // set's icons in place of legacy's bitmaps (play / pause
                    // as legacy ChangeButtonBMP swaps them, VideoBox.cpp:1414);
                    // the text stays the accessible name.
                    IconButton {
                        objectName: "playPause"
                        iconRole: root.video.playing ? "media-pause" : "media-play"
                        text: root.video.playing ? qsTr("Pause") : qsTr("Play")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Play / Pause"), "VIDEO_PLAY_PAUSE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_PLAY_PAUSE", 3, "bitmap")) root.video.togglePlay()
                    }
                    // A4: GLOBAL_PLAY_ACTUAL_LINE; legacy then focuses the
                    // Line editor's text.
                    IconButton {
                        objectName: "playActualLine"
                        iconRole: "play-line"
                        text: qsTr("Play line")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Play the current line"), "GLOBAL_PLAY_ACTUAL_LINE", 0)
                        Accessible.name: qsTr("Play the current line")
                        onClicked: {
                            if (root.hotkeyGesture("GLOBAL_PLAY_ACTUAL_LINE", 0, "bitmap"))
                                return
                            lineText.forceActiveFocus()
                            root.video.playActualLine()
                        }
                    }
                    IconButton {
                        objectName: "stopVideo"
                        iconRole: "media-stop"
                        text: qsTr("Stop")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Stop"), "VIDEO_STOP", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_STOP", 3, "bitmap")) root.video.stop()
                    }
                    IconButton {
                        objectName: "previousFrame"
                        iconRole: "frame-previous"
                        text: qsTr("Previous frame")
                        enabled: root.video.hasVideo && root.video.frame > 0
                        onClicked: root.video.stepFrames(-1)
                    }
                    Label {
                        objectName: "videoStatus"
                        text: root.video.status
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    IconButton {
                        objectName: "nextFrame"
                        iconRole: "frame-next"
                        text: qsTr("Next frame")
                        enabled: root.video.hasVideo && root.video.frame + 1 < root.video.frameCount
                        onClicked: root.video.stepFrames(1)
                    }
                }
                }
            }
        }

        KDDW.DockWidget {
            id: audioDock
            objectName: "audioDock"
            uniqueName: "Audio"
            title: qsTr("Audio")
            Panel {
                id: audioPanel
                anchors.fill: parent
                objectName: "audioPanel"
                title: qsTr("Audio")
                // The audio box in legacy AudioBox's layout: the display with
                // the search bar below it (DisplaySizer), the horizontal zoom
                // and the vertical zoom, volume and link beside them
                // (TopSizer), the button row across the bottom (ButtonSizer).
                // A1: the waveform with its time ruler and marks (legacy AudioDisplay).
                AudioDisplay {
                    id: audioDisplay
                    objectName: "audioDisplay"
                    controller: root.audio
                    visible: root.audio.loaded
                    focus: true
                    clip: true
                    anchors { left: parent.left; right: audioSliders.left; top: parent.top; bottom: audioScroll.top }
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Audio display")
                    Accessible.description: root.audio.status
                }
                ScrollBar {
                    id: audioScroll
                    objectName: "audioScroll"
                    visible: root.audio.loaded
                    orientation: Qt.Horizontal
                    policy: ScrollBar.AlwaysOn
                    focusPolicy: Qt.NoFocus
                    // legacy DisplaySizer: wxBOTTOM 4 under the search bar
                    anchors { left: parent.left; right: audioSliders.left; bottom: audioButtons.top; bottomMargin: visible ? 4 : 0 }
                    height: visible ? implicitHeight : 0
                    size: root.audio.scrollRange > 0 ? Math.min(1, root.audio.scrollPage / root.audio.scrollRange) : 1
                    Binding on position {
                        when: !audioScroll.pressed
                        value: root.audio.scrollRange > 0 ? root.audio.scrollPosition / root.audio.scrollRange : 0
                    }
                    onPositionChanged: {
                        if (pressed) // legacy AudioBox::OnScrollbar
                            root.audio.setScrollPosition(Math.round(position * root.audio.scrollRange))
                    }
                    ToolTip.text: qsTr("Search bar")
                }
                // A2: legacy AudioBox's sliders beside the display: the
                // horizontal zoom (0 at the top), the vertical zoom and the
                // volume (1 to 100, 100 at the top) and their link.
                RowLayout {
                    id: audioSliders
                    objectName: "audioSliders"
                    visible: root.audio.loaded
                    anchors { right: parent.right; top: parent.top; bottom: audioButtons.top }
                    width: visible ? implicitWidth : 0
                    spacing: 0
                    Slider {
                        objectName: "audioHorizontalZoom"
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                        from: 100; to: 0; stepSize: 1
                        value: root.audio.horizontalZoom
                        onMoved: root.audio.setHorizontalZoom(Math.round(value))
                        ToolTip.text: qsTr("Horizontal stretching")
                        ToolTip.visible: hovered
                        Accessible.name: ToolTip.text
                    }
                    ColumnLayout {
                        Layout.fillHeight: true
                        spacing: 0
                        RowLayout {
                            Layout.fillHeight: true
                            spacing: 0
                            Slider {
                                objectName: "audioVerticalZoom"
                                orientation: Qt.Vertical
                                Layout.fillHeight: true
                                from: 1; to: 100; stepSize: 1
                                value: root.audio.verticalZoom
                                onMoved: root.audio.setVerticalZoom(Math.round(value))
                                ToolTip.text: qsTr("Vertical stretching")
                                ToolTip.visible: hovered
                                Accessible.name: ToolTip.text
                            }
                            Slider {
                                objectName: "audioVolume"
                                orientation: Qt.Vertical
                                Layout.fillHeight: true
                                from: 1; to: 100; stepSize: 1
                                value: root.audio.volume
                                onMoved: root.audio.setVolume(Math.round(value))
                                ToolTip.text: qsTr("Volume")
                                ToolTip.visible: hovered
                                Accessible.name: ToolTip.text
                            }
                        }
                        // K1: the set's link icon (legacy VerticalLink's
                        // button_link bitmap, AudioBox.cpp:123).
                        ToolButton {
                            id: audioLink
                            objectName: "audioLink"
                            Layout.fillWidth: true
                            Layout.bottomMargin: 2 // legacy wxBOTTOM 2
                            text: qsTr("Link")
                            display: AbstractButton.IconOnly
                            contentItem: Icon {
                                iconRole: "link"
                                hovered: audioLink.hovered
                                pressed: audioLink.down
                            }
                            checkable: true
                            checked: root.audio.linked
                            focusPolicy: Qt.NoFocus
                            onToggled: root.audio.setLinked(checked)
                            ToolTip.text: qsTr("Link the volume and stretch sliders")
                            ToolTip.visible: hovered
                            Accessible.name: ToolTip.text
                        }
                    }
                }
                Label {
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
                        key = key + qsTr(" or ") + second
                    }
                    let tip = key.length ? text + " (" + key + ")" : text
                    tip += "\n" + qsTr("Shortcut can be set using Shift + Click")
                    if (alt)
                        tip += "\n" + qsTr("Second shortcut can be set using Control + Click")
                    return tip
                }
                // The box's buttons (legacy AudioBox's ButtonSizer, in its
                // order, with its gaps: 2 between buttons, 8 after a group).
                RowLayout {
                    id: audioButtons
                    objectName: "audioButtons"
                    visible: root.audio.loaded
                    height: visible ? implicitHeight : 0
                    spacing: 0
                    // legacy MainSizer: a 2-pixel spacer under the buttons
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: visible ? 2 : 0 }
                    // Legacy MappedButton's small square buttons; a Shift+click
                    // maps the action's Audio hotkey instead (Hotkeys::OnMapHkey).
                    // K1: each shows the set's icon in place of legacy's
                    // bitmap (AudioBox.cpp:149-193); its text is not drawn,
                    // the tip stays its accessible name and tooltip.
                    component AudioButton: ToolButton {
                        id: audioButton
                        property string symbol: ""
                        property string altSymbol: ""
                        property string tip: ""
                        property string iconRole: ""
                        property int gap: 2
                        signal run()
                        focusPolicy: Qt.NoFocus
                        padding: 2
                        display: AbstractButton.IconOnly
                        contentItem: Icon {
                            iconRole: audioButton.iconRole
                            hovered: audioButton.hovered
                            pressed: audioButton.down
                        }
                        implicitHeight: 22
                        implicitWidth: 22
                        Layout.rightMargin: gap
                        Accessible.name: tip
                        ToolTip.text: symbol.length ? audioPanel.audioTip(tip, symbol, altSymbol) : tip
                        ToolTip.visible: hovered
                        onClicked: {
                            if (symbol.length && root.hotkeyGesture(symbol, 4, true, altSymbol.length > 0))
                                return
                            run()
                        }
                    }
                    // A3, A4: AUDIO_PREVIOUS, AUDIO_NEXT, AUDIO_PLAY,
                    // AUDIO_PLAY_LINE (two hotkeys each), AUDIO_STOP
                    AudioButton {
                        objectName: "audioPrevious"; text: qsTr("Previous line")
                        iconRole: "audio-previous-line"
                        symbol: "AUDIO_PREVIOUS"; altSymbol: "AUDIO_PREVIOUS_ALT"; tip: qsTr("Play the previous line")
                        onRun: root.audio.previousLine()
                    }
                    AudioButton {
                        objectName: "audioNext"; text: qsTr("Next line")
                        iconRole: "audio-next-line"
                        symbol: "AUDIO_NEXT"; altSymbol: "AUDIO_NEXT_ALT"; tip: qsTr("Play the next line")
                        onRun: root.audio.nextLine()
                    }
                    AudioButton {
                        objectName: "audioPlay"; text: qsTr("Play")
                        iconRole: "audio-play"
                        symbol: "AUDIO_PLAY"; altSymbol: "AUDIO_PLAY_ALT"; tip: qsTr("Play the current syllable / line")
                        onRun: root.audio.runHotkey("AUDIO_PLAY")
                    }
                    AudioButton {
                        objectName: "audioPlayLine"; text: qsTr("Play line")
                        iconRole: "play-line"
                        symbol: "AUDIO_PLAY_LINE"; altSymbol: "AUDIO_PLAY_LINE_ALT"; tip: qsTr("Play the current line")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_LINE")
                    }
                    AudioButton {
                        objectName: "audioStop"; text: qsTr("Stop"); gap: 8
                        iconRole: "media-stop"
                        symbol: "AUDIO_STOP"; tip: qsTr("Stop playback")
                        onRun: root.audio.runHotkey("AUDIO_STOP")
                    }
                    // A4: the mark plays (the ruler's mark, A3)
                    AudioButton {
                        objectName: "audioPlayBeforeMark"; text: qsTr("Before mark")
                        iconRole: "play-before-mark"
                        symbol: "AUDIO_PLAY_BEFORE_MARK"; tip: qsTr("Play before the tag")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_BEFORE_MARK")
                    }
                    AudioButton {
                        objectName: "audioPlayAfterMark"; text: qsTr("After mark"); gap: 8
                        iconRole: "play-after-mark"
                        symbol: "AUDIO_PLAY_AFTER_MARK"; tip: qsTr("Play after the tag")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_AFTER_MARK")
                    }
                    // A4: the 500 ms plays and to the end
                    AudioButton {
                        objectName: "audioPlay500Before"; text: qsTr("500 before")
                        iconRole: "play-before-start"
                        symbol: "AUDIO_PLAY_500MS_BEFORE"; tip: qsTr("Play 500ms before the start time")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_500MS_BEFORE")
                    }
                    AudioButton {
                        objectName: "audioPlay500First"; text: qsTr("500 first")
                        iconRole: "play-after-start"
                        symbol: "AUDIO_PLAY_500MS_FIRST"; tip: qsTr("Play 500 ms after the start time")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_500MS_FIRST")
                    }
                    AudioButton {
                        objectName: "audioPlay500Last"; text: qsTr("500 last")
                        iconRole: "play-before-end"
                        symbol: "AUDIO_PLAY_500MS_LAST"; tip: qsTr("Play 500ms before the end time")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_500MS_LAST")
                    }
                    AudioButton {
                        objectName: "audioPlay500After"; text: qsTr("500 after")
                        iconRole: "play-after-end"
                        symbol: "AUDIO_PLAY_500MS_AFTER"; tip: qsTr("Play 500ms after the end time")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_500MS_AFTER")
                    }
                    AudioButton {
                        objectName: "audioPlayToEnd"; text: qsTr("To end"); gap: 8
                        iconRole: "play-to-end"
                        symbol: "AUDIO_PLAY_TO_END"; tip: qsTr("Play to the end")
                        onRun: root.audio.runHotkey("AUDIO_PLAY_TO_END")
                    }
                    // A3: AUDIO_LEAD_IN, AUDIO_LEAD_OUT, AUDIO_COMMIT, AUDIO_GOTO
                    AudioButton {
                        objectName: "audioLeadIn"; text: qsTr("In")
                        iconRole: "lead-in"
                        symbol: "AUDIO_LEAD_IN"; tip: qsTr("Add lead-in to the active line")
                        onRun: root.audio.leadIn()
                    }
                    AudioButton {
                        objectName: "audioLeadOut"; text: qsTr("Out"); gap: 8
                        iconRole: "lead-out"
                        symbol: "AUDIO_LEAD_OUT"; tip: qsTr("Add lead-out to the active line")
                        onRun: root.audio.leadOut()
                    }
                    AudioButton {
                        objectName: "audioCommit"; text: qsTr("Commit")
                        iconRole: "commit"
                        symbol: "AUDIO_COMMIT"; altSymbol: "AUDIO_COMMIT_ALT"; tip: qsTr("Apply changes")
                        onRun: root.audio.commit()
                    }
                    AudioButton {
                        objectName: "audioGoto"; text: qsTr("Go"); gap: 8
                        iconRole: "go-to-selection"
                        symbol: "AUDIO_GOTO"; tip: qsTr("Go to selection")
                        onRun: root.audio.goToSelection()
                    }
                    // A5: legacy AudioBox's KaraSwitch and KaraMode (toggle
                    // buttons, no hotkeys): karaoke mode and its automatic
                    // splitting; each handler focuses the display.
                    AudioButton {
                        objectName: "audioKaraoke"
                        iconRole: "karaoke"
                        text: qsTr("Karaoke")
                        checkable: true
                        checked: root.audio.karaoke
                        tip: qsTr("Enable / disable karaoke creation")
                        onToggled: {
                            root.audio.toggleKaraoke()
                            checked = Qt.binding(() => root.audio.karaoke)
                        }
                    }
                    AudioButton {
                        objectName: "audioKaraokeSplit"
                        iconRole: "karaoke-split"
                        text: qsTr("Auto split"); gap: 8
                        checkable: true
                        checked: root.audio.karaokeSplitMode
                        tip: qsTr("Enable / Disable automatic splitting of syllables")
                        onToggled: {
                            root.audio.toggleKaraokeSplitMode()
                            checked = Qt.binding(() => root.audio.karaokeSplitMode)
                        }
                    }
                    // A3: AUDIO_AUTO_COMMIT and AUDIO_NEXT_LINE_ON_COMMIT
                    // (AudioBox::OnAutoCommit / OnNextLineCommit save the
                    // option; legacy's commit never reads the second).
                    AudioButton {
                        id: audioAutoCommit
                        objectName: "audioAutoCommit"
                        iconRole: "auto-commit"
                        text: qsTr("Auto")
                        checkable: true
                        checked: root.app.settings.value("audio.autoCommit")
                        tip: qsTr("Automatically apply changes")
                        onToggled: {
                            root.app.settings.setValue("audio.autoCommit", checked)
                            audioDisplay.forceActiveFocus()
                        }
                        Connections {
                            target: root.app.settings
                            function onChanged(id) {
                                if (id === "audio.autoCommit")
                                    audioAutoCommit.checked = root.app.settings.value(id)
                            }
                        }
                    }
                    AudioButton {
                        id: audioNextCommit
                        objectName: "audioNextCommit"
                        iconRole: "next-after-commit"
                        text: qsTr("Next")
                        checkable: true
                        checked: root.app.settings.value("audio.nextLineOnCommit")
                        tip: qsTr("Go to the next line after applying changes")
                        onToggled: {
                            root.app.settings.setValue("audio.nextLineOnCommit", checked)
                            audioDisplay.forceActiveFocus()
                        }
                        Connections {
                            target: root.app.settings
                            function onChanged(id) {
                                if (id === "audio.nextLineOnCommit")
                                    audioNextCommit.checked = root.app.settings.value(id)
                            }
                        }
                    }
                    // A2: legacy AudioBox's AutoScroll, SpectrumMode and
                    // SpectrumNonLinear switches (each focuses the display, as
                    // legacy's handlers do).
                    AudioButton {
                        objectName: "audioAutoScroll"
                        iconRole: "auto-scroll"
                        text: qsTr("Auto-scroll")
                        checkable: true
                        checked: root.audio.autoScroll
                        tip: qsTr("Auto-scroll to the active line")
                        onToggled: { root.audio.setAutoScroll(checked); audioDisplay.forceActiveFocus() }
                    }
                    AudioButton {
                        objectName: "audioSpectrumMode"
                        iconRole: "spectrum"
                        text: qsTr("Spectrum")
                        checkable: true
                        checked: root.audio.spectrumOn
                        tip: qsTr("Spectrum mode")
                        onToggled: { root.audio.setSpectrumOn(checked); audioDisplay.forceActiveFocus() }
                    }
                    AudioButton {
                        objectName: "audioSpectrumNonLinear"
                        iconRole: "spectrum-nonlinear"
                        text: qsTr("Speech")
                        checkable: true
                        checked: root.audio.spectrumNonLinear
                        tip: qsTr("Enhance speech frequencies in the spectrum")
                        onToggled: { root.audio.setSpectrumNonLinear(checked); audioDisplay.forceActiveFocus() }
                    }
                    Item { Layout.fillWidth: true }
                }
                // Legacy AudioBox's accelerator table (AudioBox::SetAccels:
                // its window's bindings in the hotkey registry, O2), acting
                // while the focus is in the box; the registry routes the key
                // (HotkeysController::actionFor), so a remapped key acts and
                // the old one no longer does.
                Keys.onShortcutOverride: event => {
                    event.accepted = root.audio.loaded && root.hotkeys.actionFor(4, event.key, event.modifiers) !== ""
                }
                Keys.onPressed: event => {
                    if (!root.audio.loaded)
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

        KDDW.DockWidget {
            id: editorDock
            objectName: "editorDock"
            uniqueName: "Editor"
            title: qsTr("Line editor")
            Panel {
                id: editorPanel
                anchors.fill: parent
                objectName: "editorPanel"
                title: shell.hasEditingTarget ? qsTr("Line editor: %1").arg(shell.editingTitle)
                                              : qsTr("Line editor")
                // O2: EditBox's accelerator table covers the whole Line editor
                // (TabPanel::SetAccels): a key its other controls do not take
                // (the time and margin fields keep their editing keys) runs
                // the Editor binding; the text fields route their own first.
                // The commit keys stay the fields' own (OnNewline's rule for
                // the time fields, EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,
                // EditBox.cpp:987-999, is not in the rewrite yet).
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
                    root.runEditorHotkey(action, translationText.activeFocus ? translationText : lineText)
                    event.accepted = true
                }
                ColumnLayout {
                    anchors.fill: parent

                    // Local inspector: timing and margins of the active Line.
                    RowLayout {
                        Layout.fillWidth: true
                        component Field: TextField {
                            property string value
                            text: value
                            enabled: root.editor.editable
                            selectByMouse: true
                            Layout.preferredWidth: 90
                            onValueChanged: text = value
                        }
                        Field {
                            objectName: "startField"
                            value: root.editor.startText
                            Accessible.name: qsTr("Start")
                            onEditingFinished: root.editor.setStartText(text)
                        }
                        Field {
                            objectName: "endField"
                            value: root.editor.endText
                            Accessible.name: qsTr("End")
                            onEditingFinished: root.editor.setEndText(text)
                        }
                        Field {
                            objectName: "marginLeftField"
                            value: root.editor.marginLeftText
                            Layout.preferredWidth: 50
                            Accessible.name: qsTr("Left margin")
                            onEditingFinished: root.editor.setMarginText(0, text)
                        }
                        Field {
                            objectName: "marginRightField"
                            value: root.editor.marginRightText
                            Layout.preferredWidth: 50
                            Accessible.name: qsTr("Right margin")
                            onEditingFinished: root.editor.setMarginText(1, text)
                        }
                        Field {
                            objectName: "marginVerticalField"
                            value: root.editor.marginVerticalText
                            Layout.preferredWidth: 50
                            Accessible.name: qsTr("Vertical margin")
                            onEditingFinished: root.editor.setMarginText(2, text)
                        }
                        // Ordinary ASS controls; they keep focus (and the
                        // selection) in the text field.
                        Repeater {
                            // O2: mapped buttons (EDITBOX_INSERT_BOLD ...): Shift+click
                            // maps the hotkey, the tooltip shows it.
                            // K1: the set's icons in place of the letters
                            // (legacy EditBox's BOLD, ITALIC, UNDER, STRIKE
                            // bitmaps, EditBox.cpp:168-195).
                            model: [
                                { tag: "b", name: qsTr("Bold"), symbol: "EDITBOX_INSERT_BOLD" },
                                { tag: "i", name: qsTr("Italic"), symbol: "EDITBOX_INSERT_ITALIC" },
                                { tag: "u", name: qsTr("Underline"), symbol: "EDITBOX_CHANGE_UNDERLINE" },
                                { tag: "s", name: qsTr("Strikeout"), symbol: "EDITBOX_CHANGE_STRIKEOUT" }
                            ]
                            IconToolButton {
                                required property var modelData
                                required property int index
                                objectName: "tag_" + modelData.tag
                                iconRole: ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout"][index]
                                text: modelData.name
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                tip: root.mappedTip(modelData.name, modelData.symbol, 2)
                                onClicked: {
                                    if (root.hotkeyGesture(modelData.symbol, 2, true))
                                        return
                                    const field = translationText.activeFocus ? translationText : lineText
                                    root.editor.toggleTagIn(field.role, modelData.tag, field.selectionStart, field.selectionEnd)
                                }
                            }
                        }
                        // E1: Font selection and the four colours
                        // (EDITBOX_CHANGE_FONT, EDITBOX_CHANGE_COLOR_*).
                        IconToolButton {
                            objectName: "changeFont"
                            iconRole: "tag-font"
                            text: qsTr("Font selection")
                            focusPolicy: Qt.NoFocus
                            enabled: root.editor.editable
                            tip: root.mappedTip(qsTr("Font selection"), "EDITBOX_CHANGE_FONT", 2)
                            onClicked: {
                                if (root.hotkeyGesture("EDITBOX_CHANGE_FONT", 2, true))
                                    return
                                const field = translationText.activeFocus ? translationText : lineText
                                fontDialog.openFor(field.role, field.selectionStart, field.selectionEnd)
                            }
                        }
                        Repeater {
                            model: [
                                { number: 1, name: qsTr("Primary color"), symbol: "EDITBOX_CHANGE_COLOR_PRIMARY" },
                                { number: 2, name: qsTr("Secondary color for karaoke"), symbol: "EDITBOX_CHANGE_COLOR_SECONDARY" },
                                { number: 3, name: qsTr("Border color"), symbol: "EDITBOX_CHANGE_COLOR_OUTLINE" },
                                { number: 4, name: qsTr("Shadow color"), symbol: "EDITBOX_CHANGE_COLOR_SHADOW" }
                            ]
                            IconToolButton {
                                required property var modelData
                                objectName: "changeColour" + modelData.number
                                iconRole: ["colour-primary", "colour-secondary", "colour-outline", "colour-shadow"][modelData.number - 1]
                                text: modelData.name
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                tip: root.mappedTip(modelData.name, modelData.symbol, 2)
                                onClicked: {
                                    if (root.hotkeyGesture(modelData.symbol, 2, true))
                                        return
                                    const field = translationText.activeFocus ? translationText : lineText
                                    if (colourDialog.openFor(modelData.number, field.role, field.selectionStart, field.selectionEnd))
                                        root.app.colourPickerOpened()
                                }
                            }
                        }
                        // E2: custom tag buttons; right click (or a button
                        // without a tag) edits it.
                        Repeater {
                            model: root.tagButtons.buttons
                            ToolButton {
                                required property var modelData
                                required property int index
                                objectName: "tagButton" + index
                                text: modelData.name
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                Accessible.name: modelData.name
                                Accessible.description: modelData.tag
                                ToolTip.visible: hovered && modelData.tag.length > 0
                                ToolTip.text: root.mappedTip(modelData.tag, "EDITBOX_TAG_BUTTON" + (index + 1), 2)
                                onClicked: {
                                    if (root.hotkeyGesture("EDITBOX_TAG_BUTTON" + (index + 1), 2, true))
                                        return
                                    if (modelData.tag.length === 0)
                                        tagButtonDialog.editButton(index)
                                    else
                                        root.applyTagButton(index)
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: tagButtonDialog.editButton(index)
                                }
                            }
                        }
                        ToolButton {
                            objectName: "manageTagButtons"
                            text: qsTr("Manage tag buttons")
                            focusPolicy: Qt.NoFocus
                            onClicked: tagButtonsMenu.popup()
                            ShellMenu {
                                id: tagButtonsMenu
                                Instantiator {
                                    model: root.tagButtons.buttons
                                    delegate: ShellMenuItem {
                                        required property var modelData
                                        required property int index
                                        text: modelData.name
                                        onTriggered: root.applyTagButton(index)
                                    }
                                    onObjectAdded: (index, object) => tagButtonsMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => tagButtonsMenu.removeItem(object)
                                }
                                ShellMenuItem {
                                    objectName: "changeTagButtonCount"
                                    text: qsTr("Change number of buttons")
                                    onTriggered: tagButtonCountDialog.open()
                                }
                            }
                        }
                        CheckBox {
                            objectName: "showTags"
                            text: qsTr("Show tags")
                            checked: root.editor.showTags
                            onToggled: root.editor.showTags = checked
                        }
                    }

                    RoleField {
                        id: lineText
                        objectName: "lineText"
                        role: 0
                        focus: true
                        Accessible.name: root.editor.translationMode ? qsTr("Original text") : qsTr("Line text")
                    }
                    RoleField {
                        id: translationText
                        objectName: "translationText"
                        role: 1
                        visible: root.editor.translationMode
                        Accessible.name: qsTr("Translated text")
                    }
                    // Legacy translation-mode buttons (EDITBOX_PASTE_*,
                    // EDITBOX_HIDE_ORIGINAL renamed Comment out original).
                    RowLayout {
                        visible: root.editor.translationMode
                        Button {
                            objectName: "pasteAllToTranslation"
                            text: qsTr("Paste all")
                            focusPolicy: Qt.NoFocus
                            enabled: root.editor.editable
                            ToolTip.visible: hovered
                            ToolTip.text: root.mappedTip(text, "EDITBOX_PASTE_ALL_TO_TRANSLATION", 2)
                            onClicked: if (!root.hotkeyGesture("EDITBOX_PASTE_ALL_TO_TRANSLATION", 2, true)) root.editor.pasteAllToTranslation()
                        }
                        Button {
                            objectName: "pasteSelectionToTranslation"
                            text: qsTr("Paste the selected")
                            focusPolicy: Qt.NoFocus
                            enabled: root.editor.editable
                            ToolTip.visible: hovered
                            ToolTip.text: root.mappedTip(text, "EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2)
                            onClicked: {
                                if (root.hotkeyGesture("EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2, true))
                                    return
                                root.editor.pasteSelectionToTranslation(lineText.selectionStart, lineText.selectionEnd,
                                                                        translationText.cursorPosition)
                            }
                        }
                        Button {
                            objectName: "commentOutOriginal"
                            text: qsTr("Comment out original")
                            focusPolicy: Qt.NoFocus
                            enabled: root.editor.editable
                            ToolTip.visible: hovered
                            ToolTip.text: root.mappedTip(text, "EDITBOX_HIDE_ORIGINAL", 2)
                            onClicked: if (!root.hotkeyGesture("EDITBOX_HIDE_ORIGINAL", 2, true)) root.editor.commentOutOriginal()
                        }
                    }

                    Label {
                        objectName: "editorProblem"
                        visible: text.length > 0
                        text: root.editor.problem
                        color: Theme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.AlertMessage
                    }
                    Label {
                        objectName: "editorAttempted"
                        visible: root.editor.attempted.length > 0
                        text: qsTr("Not applied: %1").arg(root.editor.attempted)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        KDDW.DockWidget {
            id: gridDock
            objectName: "gridDock"
            uniqueName: "Grid"
            title: qsTr("Grid")
            Panel {
                id: gridPanel
                anchors.fill: parent
                objectName: "gridPanel"
                title: shell.hasEditingTarget ? qsTr("Editing: %1").arg(shell.editingTitle) : qsTr("No document open")
                accessibleName: qsTr("Grid")
                focus: true
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
                    onLineClicked: (id, modifiers) => root.app.clickLine(id, modifiers)
                    onLineDragged: id => root.app.dragSelection(id)
                    onSelectAllRequested: root.app.selectAllLines()
                    onContextMenuRequested: (x, y) => gridMenu.popup(grid, x, y)
                    onHiddenBlockToggleRequested: row => root.app.toggleHiddenBlock(row)
                    onGroupToggleRequested: id => root.app.toggleGroup(id)
                    onGroupMenuRequested: (id, x, y) => {
                        groupMenu.description = id
                        groupMenu.popup(grid, x, y)
                    }
                    // Legacy tree description menu (ContextMenuTree).
                    ShellMenu {
                        id: groupMenu
                        objectName: "groupMenu"
                        property var description: 0
                        ShellMenuItem { objectName: "groupAddLines"; text: qsTr("Add lines"); onTriggered: root.app.addLinesToGroup(groupMenu.description) }
                        ShellMenuItem { objectName: "groupCopy"; text: qsTr("Copy tree"); onTriggered: root.app.copyGroup(groupMenu.description) }
                        ShellMenuItem {
                            objectName: "groupRename"; text: qsTr("Change description")
                            onTriggered: groupDescriptionDialog.edit(groupMenu.description)
                        }
                        ShellMenuItem { objectName: "groupSelect"; text: qsTr("Select tree lines"); onTriggered: root.app.selectGroup(groupMenu.description) }
                        ShellMenuItem { objectName: "groupDelete"; text: qsTr("Delete"); onTriggered: root.app.removeGroup(groupMenu.description) }
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
                            title: qsTr("&Insert")
                            ShellMenuItem { objectName: "insertBefore"; text: qsTr("Insert &before"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE")) root.app.insertLine(true) }
                            ShellMenuItem { objectName: "insertAfter"; text: qsTr("Insert &after"); onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER")) root.app.insertLine(false) }
                            ShellMenuItem {
                                objectName: "insertBeforeVideo"; text: qsTr("Insert before with &video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_VIDEO")) root.app.insertLine(true, "video")
                            }
                            ShellMenuItem {
                                objectName: "insertAfterVideo"; text: qsTr("Insert after with video time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_VIDEO")) root.app.insertLine(false, "video")
                            }
                            ShellMenuItem {
                                objectName: "insertBeforeFrame"; text: qsTr("Insert before with video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_BEFORE_WITH_VIDEO_FRAME")) root.app.insertLine(true, "frame")
                            }
                            ShellMenuItem {
                                objectName: "insertAfterFrame"; text: qsTr("Insert after with video frame time")
                                enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_INSERT_AFTER_WITH_VIDEO_FRAME")) root.app.insertLine(false, "frame")
                            }
                        }
                        ShellMenuItem {
                            objectName: "duplicateLines"
                            // SetAccMenu: the binding's keys after the tab.
                            readonly property string keys: root.boundKeys("GRID_DUPLICATE_LINES", 1)
                            text: qsTr("&Duplicate lines") + (keys.length ? "\t" + keys : "")
                            onTriggered: if (!root.gridGesture(this, "GRID_DUPLICATE_LINES")) root.app.duplicateLines()
                        }
                        ShellMenuItem { objectName: "swapLines"; text: qsTr("&Swap"); onTriggered: if (!root.gridGesture(this, "GRID_SWAP_LINES")) root.app.swapLines() }
                        ShellMenuItem { objectName: "joinLines"; text: qsTr("Join &lines"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_LINES")) root.app.joinLines("join") }
                        ShellMenuItem { objectName: "joinFirst"; text: qsTr("Join lines and keep first"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_FIRST_LINE")) root.app.joinLines("first") }
                        ShellMenuItem { objectName: "joinLast"; text: qsTr("Join lines and keep last"); onTriggered: if (!root.gridGesture(this, "GRID_JOIN_TO_LAST_LINE")) root.app.joinLines("last") }
                        ShellMenuItem {
                            objectName: "continuousPrevious"; text: qsTr("Set times as a continuous (previous line)")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_PREVIOUS_LINE")) root.app.makeContinuous(true)
                        }
                        ShellMenuItem {
                            objectName: "continuousNext"; text: qsTr("Set times as a continuous (next line)")
                            onTriggered: if (!root.gridGesture(this, "GRID_MAKE_CONTINOUS_NEXT_LINE")) root.app.makeContinuous(false)
                        }
                        // Legacy "Split lines" (GRID_SPLIT_BY_*).
                        ShellMenu {
                            title: qsTr("Split lines")
                            ShellMenuItem {
                                objectName: "splitAtVideoTime"
                                text: qsTr("Split line at video time")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_VIDEO_TIME")) root.app.splitLines("videoTime")
                            }
                            ShellMenuItem {
                                objectName: "splitIntoFrames"
                                text: qsTr("Split lines into frames")
                                enabled: root.video.hasVideo
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_FRAME")) root.app.splitLines("frames")
                            }
                            ShellMenuItem {
                                objectName: "splitIntoCharacters"
                                text: qsTr("Split lines into characters")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_CHARS")) root.app.splitLines("chars")
                            }
                            ShellMenuItem {
                                objectName: "splitIntoWords"
                                text: qsTr("Split lines into words")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WORDS")) root.app.splitLines("words")
                            }
                            ShellMenuItem {
                                objectName: "splitByWraps"
                                text: qsTr("Split lines by wraps")
                                onTriggered: if (!root.gridGesture(this, "GRID_SPLIT_BY_WRAPS")) root.app.splitLines("wraps")
                            }
                        }
                        ShellMenuItem { objectName: "makeTree"; text: qsTr("Make tree"); onTriggered: if (!root.gridGesture(this, "GRID_TREE_MAKE")) root.app.makeGroups() }
                        // E3: GRID_PASTE_TRANSLATION and GRID_TRANSLATION_DIALOG.
                        ShellMenuItem {
                            objectName: "pasteTranslation"
                            text: qsTr("Paste translation text")
                            enabled: gridMenu.canPasteTranslation
                            onTriggered: {
                                if (root.gridGesture(this, "GRID_PASTE_TRANSLATION"))
                                    return
                                translationFileDialog.currentFolder = root.app.targetFolder()
                                translationFileDialog.open()
                            }
                        }
                        ShellMenuItem {
                            objectName: "translationDialog"
                            text: qsTr("Dialogue shifting window")
                            enabled: gridMenu.canShiftTranslation
                            onTriggered: if (!root.gridGesture(this, "GRID_TRANSLATION_DIALOG")) translationShiftWindow.show()
                        }
                        ShellMenuItem { objectName: "hideSelectedLines"; text: qsTr("Hide selected lines"); onTriggered: if (!root.gridGesture(this, "GRID_HIDE_SELECTED")) root.app.hideSelectedLines() }
                        // Legacy Filtering submenu (GRID_FILTER_*).
                        ShellMenu {
                            id: filteringMenu
                            objectName: "filteringMenu"
                            title: qsTr("Filtering")
                            property var styleNames: []
                            onAboutToShow: styleNames = root.app.styleNames()
                            ShellMenuItem {
                                objectName: "filterAfterLoad"
                                text: qsTr("Filter after loading subtitles")
                                checkable: true
                                enabled: root.shell.assColumns
                                checked: root.gridFilter.afterLoad
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_AFTER_SUBS_LOAD")) root.gridFilter.afterLoad = checked
                            }
                            ShellMenuItem {
                                objectName: "filterInvert"
                                text: qsTr("Reverse filtering")
                                checkable: true
                                checked: root.gridFilter.inverted
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_INVERT")) root.gridFilter.inverted = checked
                            }
                            ShellMenuItem {
                                objectName: "filterDoNotReset"
                                text: qsTr("Do not reset previous filtering")
                                checkable: true
                                checked: root.gridFilter.addToFilter
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_DO_NOT_RESET")) root.gridFilter.addToFilter = checked
                            }
                            ShellMenu {
                                id: filterStylesMenu
                                title: qsTr("Hide lines with styles")
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
                                    { bit: 2, label: qsTr("Hide selected lines"), name: "filterBySelection",
                                      symbol: "GRID_FILTER_BY_SELECTIONS" },
                                    { bit: 4, label: qsTr("Hide comments"), name: "filterByComments", ass: true,
                                      symbol: "GRID_FILTER_BY_DIALOGUES" },
                                    { bit: 8, label: qsTr("Show unconfirmed"), name: "filterByUnconfirmed", tl: true,
                                      symbol: "GRID_FILTER_BY_DOUBTFUL" },
                                    { bit: 16, label: qsTr("Show untranslated"), name: "filterByUntranslated", tl: true,
                                      symbol: "GRID_FILTER_BY_UNTRANSLATED" }
                                ]
                                delegate: ShellMenuItem {
                                    required property var modelData
                                    objectName: modelData.name
                                    text: modelData.label
                                    checkable: true
                                    enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.tl || root.editor.translationMode)
                                    checked: (root.gridFilter.filterBy & modelData.bit) !== 0
                                    onTriggered: if (!root.gridGesture(this, modelData.symbol)) root.gridFilter.setFilterBy(modelData.bit, checked)
                                }
                                onObjectAdded: (index, object) => filteringMenu.insertItem(4 + index, object)
                                onObjectRemoved: (index, object) => filteringMenu.removeItem(object)
                            }
                            ShellMenuItem { objectName: "filter"; text: qsTr("Filter"); onTriggered: if (!root.gridGesture(this, "GRID_FILTER")) root.app.filterLines() }
                            ShellMenuItem {
                                objectName: "turnOffFiltering"
                                text: qsTr("Turn off filtering")
                                enabled: root.shell.filtered
                                onTriggered: if (!root.gridGesture(this, "GRID_FILTER_BY_NOTHING")) root.app.turnOffFiltering()
                            }
                        }
                        ShellMenuItem {
                            objectName: "ignoreFilteringInActions"
                            text: qsTr("Ignore filtering in some actions")
                            checkable: true
                            checked: root.gridFilter.ignoreInActions
                            onTriggered: if (!root.gridGesture(this, "GRID_FILTER_IGNORE_IN_ACTIONS")) root.gridFilter.ignoreInActions = checked
                        }
                        // Legacy "Hide columns" (GRID_HIDE_LAYER ... GRID_HIDE_WRAPS).
                        ShellMenu {
                            id: hideColumnsMenu
                            objectName: "hideColumnsMenu"
                            title: qsTr("Hide columns")
                            Instantiator {
                                model: [
                                    { bit: 1, label: qsTr("Hide layer"), ass: true, symbol: "GRID_HIDE_LAYER" },
                                    { bit: 2, label: qsTr("Hide start time"), ass: false, symbol: "GRID_HIDE_START" },
                                    { bit: 4, label: qsTr("Hide end time"), ass: false, end: true, symbol: "GRID_HIDE_END" },
                                    { bit: 16, label: qsTr("Hide actor"), ass: true, symbol: "GRID_HIDE_ACTOR" },
                                    { bit: 8, label: qsTr("Hide style"), ass: true, symbol: "GRID_HIDE_STYLE" },
                                    { bit: 32, label: qsTr("Hide left margin"), ass: true, symbol: "GRID_HIDE_MARGINL" },
                                    { bit: 64, label: qsTr("Hide right margin"), ass: true, symbol: "GRID_HIDE_MARGINR" },
                                    { bit: 128, label: qsTr("Hide vertical margin"), ass: true, symbol: "GRID_HIDE_MARGINV" },
                                    { bit: 256, label: qsTr("Hide effect"), ass: true, symbol: "GRID_HIDE_EFFECT" },
                                    { bit: 512, label: qsTr("Hide characters per second"), ass: false, symbol: "GRID_HIDE_CPS" },
                                    { bit: 8192, label: qsTr("Hide line wraps"), ass: false, symbol: "GRID_HIDE_WRAPS" }
                                ]
                                delegate: ShellMenuItem {
                                    required property var modelData
                                    objectName: "hideColumn" + modelData.bit
                                    text: modelData.label
                                    checkable: true
                                    checked: (root.shell.hiddenColumns & modelData.bit) !== 0
                                    enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.end || root.shell.endColumn)
                                    onTriggered: if (!root.gridGesture(this, modelData.symbol)) root.shell.toggleColumn(modelData.bit)
                                }
                                onObjectAdded: (index, object) => hideColumnsMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => hideColumnsMenu.removeItem(object)
                            }
                        }
                        ShellMenuItem { objectName: "setNewFps"; text: qsTr("Set new FPS"); onTriggered: if (!root.gridGesture(this, "GRID_SET_NEW_FPS")) fpsWindow.show() }
                        ShellMenuItem {
                            objectName: "setFpsFromVideo"; text: qsTr("Set FPS from video")
                            enabled: root.video.hasVideo; onTriggered: if (!root.gridGesture(this, "GRID_SET_FPS_FROM_VIDEO")) root.app.setFpsFromVideo()
                        }
                        ShellMenuItem { objectName: "copyLines"; text: qsTr("Copy\tCtrl+C"); onTriggered: if (!root.gridGesture(this, "GRID_COPY")) root.app.copyLines() }
                        ShellMenuItem { objectName: "cutLines"; text: qsTr("Cut\tCtrl+X"); onTriggered: if (!root.gridGesture(this, "GRID_CUT")) root.app.cutLines() }
                        ShellMenuItem { objectName: "pasteLines"; text: qsTr("Paste\tCtrl+V"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE")) root.app.pasteLines() }
                        ShellMenuItem { objectName: "copyColumns"; text: qsTr("Copy columns"); onTriggered: if (!root.gridGesture(this, "GRID_COPY_COLUMNS")) columnsWindow.choose(false) }
                        ShellMenuItem { objectName: "pasteColumns"; text: qsTr("Paste columns"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE_COLUMNS")) columnsWindow.choose(true) }
                        ShellMenuItem { objectName: "deleteLines"; text: qsTr("Delete lines\tShift+Del"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_LINES")) root.app.deleteLines() }
                        // Y8: SubsGrid's menu (SubsGrid.cpp:286-288).
                        MenuSeparator {}
                        ShellMenuItem {
                            objectName: "gridFontCollector"; text: qsTr("Font collector"); enabled: root.shell.assColumns
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_OPEN_FONT_COLLECTOR")) fontCollectorDialog.showOnce()
                        }
                        // Y9: SubsGrid.cpp:289, enabled for a ".mkv" or ".ogm" video.
                        ShellMenuItem {
                            objectName: "gridSubsFromMkv"; text: qsTr("Load subtitles from an MKV/OGM file")
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
                    text: qsTr("No document open")
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
            title: shell.hasReference ? qsTr("Reference: %1").arg(shell.referenceTitle) : qsTr("Reference")
            Panel {
                id: referencePanel
                anchors.fill: parent
                objectName: "referencePanel"
                visible: shell.hasReference
                title: qsTr("Reference (protected, read-only): %1").arg(shell.referenceTitle)
                HikariGrid {
                    objectName: "referenceGrid"
                    anchors.fill: parent
                    focus: true
                    Accessible.role: Accessible.Table // in the accessibility tree, as the Grid
                    model: shell.referenceLines
                }
            }
        }

        // F5: the Timing tool (legacy ShiftTimes panel).
        KDDW.DockWidget {
            id: timingDock
            objectName: "timingDock"
            uniqueName: "Timing"
            title: qsTr("Timing")
            Panel {
                id: timingPanel
                objectName: "timingPanel"
                anchors.fill: parent
                title: qsTr("Shift times")
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    ColumnLayout {
                        id: shiftForm
                        width: timingPanel.width - 20
                        readonly property var settings: root.shiftTimes.settings
                        function set(key, value) {
                            const next = Object.assign({}, settings)
                            next[key] = value
                            root.shiftTimes.settings = next
                        }
                        // Legacy time control: h:mm:ss.cc, or frames when shown as frames.
                        function timeText(ms) {
                            const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
                            const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
                            return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
                        }
                        function parseTime(text) {
                            const m = /^(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?$/.exec(text.trim())
                            if (!m)
                                return -1
                            return ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 1000 + (m[4] ? +m[4].padEnd(2, "0") * 10 : 0)
                        }
                        RowLayout {
                            Label { text: shiftForm.settings.byFrames ? qsTr("Frames") : qsTr("Time") }
                            TextField {
                                id: shiftTime
                                objectName: "shiftTime"
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Shift subtitle times")
                                text: shiftForm.settings.byFrames ? String(shiftForm.settings.frames)
                                                                  : shiftForm.timeText(shiftForm.settings.timeMs)
                                onEditingFinished: {
                                    if (shiftForm.settings.byFrames) {
                                        shiftForm.set("frames", Math.max(0, parseInt(text) || 0))
                                    } else {
                                        const ms = shiftForm.parseTime(text)
                                        if (ms >= 0)
                                            shiftForm.set("timeMs", ms)
                                    }
                                }
                            }
                            Button {
                                objectName: "shiftApply"
                                text: qsTr("Shift")
                                Accessible.description: qsTr("Shift subtitle times")
                                onClicked: root.runShiftTimes()
                            }
                        }
                        RowLayout {
                            RadioButton { objectName: "shiftForward"; text: qsTr("Forward"); checked: shiftForm.settings.forward; onToggled: if (checked) shiftForm.set("forward", true) }
                            RadioButton { objectName: "shiftBackward"; text: qsTr("Backward"); checked: !shiftForm.settings.forward; onToggled: if (checked) shiftForm.set("forward", false) }
                        }
                        RowLayout {
                            CheckBox {
                                objectName: "shiftFrames"
                                text: qsTr("Frames")
                                checked: shiftForm.settings.byFrames
                                enabled: checked || root.app.exactTimebase()
                                onToggled: shiftForm.set("byFrames", checked)
                            }
                            CheckBox { objectName: "shiftTagTimes"; text: qsTr("Tag times"); checked: shiftForm.settings.tagTimes; onToggled: shiftForm.set("tagTimes", checked) }
                        }
                        GroupBox {
                            title: qsTr("Shift by video / audio")
                            Layout.fillWidth: true
                            ColumnLayout {
                                RowLayout {
                                    RadioButton { text: qsTr("Beginning"); checked: shiftForm.settings.fromStartTime; onToggled: if (checked) shiftForm.set("fromStartTime", true) }
                                    RadioButton { text: qsTr("End"); checked: !shiftForm.settings.fromStartTime; onToggled: if (checked) shiftForm.set("fromStartTime", false) }
                                }
                                CheckBox { objectName: "shiftToVideo"; text: qsTr("Move the marker to video time"); checked: shiftForm.settings.moveToVideoTime; onToggled: shiftForm.set("moveToVideoTime", checked) }
                                CheckBox { objectName: "shiftToAudio"; text: qsTr("Move the marker to audio time"); enabled: root.audio.hasMark /* A3: ShiftTimes::Contents */; checked: shiftForm.settings.moveToAudioTime; onToggled: shiftForm.set("moveToAudioTime", checked) }
                            }
                        }
                        Label { text: qsTr("Which lines") }
                        ComboBox {
                            objectName: "shiftWhichLines"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Which lines")
                            model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From the selected line"),
                                    qsTr("All times higher and equal"), qsTr("All times lower and equal"),
                                    qsTr("According to the selected styles")]
                            currentIndex: shiftForm.settings.whichLines
                            onActivated: (index) => shiftForm.set("whichLines", index)
                        }
                        TextField {
                            objectName: "shiftStyles"
                            Layout.fillWidth: true
                            visible: shiftForm.settings.whichLines === 5
                            placeholderText: qsTr("Styles, separated by commas")
                            Accessible.name: qsTr("Styles")
                            text: shiftForm.settings.styles
                            onEditingFinished: shiftForm.set("styles", text)
                        }
                        Label { text: qsTr("A method of time shift") }
                        ComboBox {
                            objectName: "shiftWhichTimes"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("A method of time shift")
                            model: [qsTr("Both times"), qsTr("The starting time"), qsTr("End time")]
                            currentIndex: shiftForm.settings.whichTimes
                            onActivated: (index) => shiftForm.set("whichTimes", index)
                        }
                        Label { text: qsTr("Correction end times") }
                        ComboBox {
                            objectName: "shiftEndCorrection"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Correction end times")
                            model: [qsTr("Leave unchanged"), qsTr("Adjust overlapping times"), qsTr("New times")]
                            currentIndex: shiftForm.settings.correctEndTimes
                            onActivated: (index) => shiftForm.set("correctEndTimes", index)
                        }
                        // F6: legacy "Post processor" (the panel switches between shift and postprocessor).
                        CheckBox {
                            objectName: "postprocessorOn"
                            text: qsTr("Run post processor")
                            checked: (shiftForm.settings.postprocessor & 16) !== 0
                            onToggled: shiftForm.set("postprocessor", checked ? (shiftForm.settings.postprocessor | 16)
                                                                              : (shiftForm.settings.postprocessor & ~16))
                        }
                        GroupBox {
                            title: qsTr("Post processor")
                            Layout.fillWidth: true
                            visible: (shiftForm.settings.postprocessor & 16) !== 0
                            GridLayout {
                                anchors.fill: parent
                                columns: 2
                                component FlagBox: CheckBox {
                                    property int bit
                                    checked: (shiftForm.settings.postprocessor & bit) !== 0
                                    onToggled: shiftForm.set("postprocessor", checked ? (shiftForm.settings.postprocessor | bit)
                                                                                      : (shiftForm.settings.postprocessor & ~bit))
                                }
                                component MsBox: SpinBox {
                                    property string key
                                    from: 0; to: 100000; stepSize: 10; editable: true
                                    value: shiftForm.settings[key]
                                    onValueModified: shiftForm.set(key, value)
                                }
                                FlagBox { objectName: "ppLeadIn"; bit: 1; text: qsTr("Lead-in") }
                                MsBox { key: "leadIn"; Accessible.name: qsTr("Lead-in") }
                                FlagBox { objectName: "ppLeadOut"; bit: 2; text: qsTr("Lead-out") }
                                MsBox { key: "leadOut"; Accessible.name: qsTr("Lead-out") }
                                FlagBox { objectName: "ppContinuous"; bit: 4; text: qsTr("Set times as continuous"); Layout.columnSpan: 2 }
                                Label { text: qsTr("Start time threshold") }
                                MsBox { key: "thresholdStart"; Accessible.name: qsTr("Start time threshold") }
                                Label { text: qsTr("End time threshold") }
                                MsBox { key: "thresholdEnd"; Accessible.name: qsTr("End time threshold") }
                                FlagBox { objectName: "ppSnap"; bit: 8; text: qsTr("Snap to keyframes"); enabled: root.app.exactTimebase(); Layout.columnSpan: 2 }
                                Label { text: qsTr("Before the start of time") }
                                MsBox { key: "keyframeBeforeStart"; Accessible.name: qsTr("Before the start of time") }
                                Label { text: qsTr("After the start time") }
                                MsBox { key: "keyframeAfterStart"; Accessible.name: qsTr("After the start time") }
                                Label { text: qsTr("Before the end time") }
                                MsBox { key: "keyframeBeforeEnd"; Accessible.name: qsTr("Before the end time") }
                                Label { text: qsTr("After the end time") }
                                MsBox { key: "keyframeAfterEnd"; Accessible.name: qsTr("After the end time") }
                            }
                        }
                        GroupBox {
                            title: qsTr("Profiles")
                            Layout.fillWidth: true
                            RowLayout {
                                anchors.fill: parent
                                ComboBox {
                                    id: shiftProfile
                                    objectName: "shiftProfile"
                                    Layout.fillWidth: true
                                    Accessible.name: qsTr("Profiles")
                                    editable: true
                                    model: root.shiftTimes.profiles
                                    onActivated: (index) => root.shiftTimes.loadProfile(textAt(index))
                                }
                                Button { objectName: "shiftProfileSave"; text: "+"; Accessible.name: qsTr("Adding and editing profiles"); onClicked: root.shiftTimes.saveProfile(shiftProfile.editText) }
                                Button { objectName: "shiftProfileRemove"; text: "-"; Accessible.name: qsTr("Removing profiles"); onClicked: root.shiftTimes.removeProfile(shiftProfile.editText) }
                            }
                        }
                        Label {
                            id: shiftMessage
                            objectName: "shiftMessage"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: palette.highlight
                            Accessible.role: Accessible.AlertMessage
                        }
                    }
                }
            }
        }

        // F1: the Search tool (find and replace; docs/qt/ux/reviewed-surface-layouts.md B).
        KDDW.DockWidget {
            id: searchDock
            objectName: "searchDock"
            uniqueName: "Search"
            title: qsTr("Search")
            // TabWindow::SaveValues when the tool closes.
            onIsOpenChanged: if (!isOpen) searchTool.save()
            Panel {
                id: searchPanel
                objectName: "searchPanel"
                anchors.fill: parent
                title: qsTr("Find and replace")
                onActiveFocusChanged: if (activeFocus) searchTool.activated()
                SearchTool {
                    id: searchTool
                    anchors.fill: parent
                    app: root.app
                }
            }
        }

        Component.onCompleted: {
            root.defaultLayout()
            // Once the arrangement is laid out, give the audio box legacy's
            // height whatever this platform's fonts make the panel chrome,
            // then keep that as the default (Reset layout) before a saved
            // layout replaces it.
            Qt.callLater(function() {
                root.fitAudioBox()
                root.workspaceLayout.captureDefault()
                root.workspaceLayout.restoreSaved()
            })
        }
    }
    // Completed layout operations are saved, not every drag (D1): a cheap
    // periodic check writes only when the arrangement changed.
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.workspaceLayout.save()
    }

    // D1: a layout that could not be restored is named here, with the way back.
    header: Pane {
        objectName: "layoutNotice"
        visible: root.workspaceLayout.notice.length > 0
        padding: 6
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
                text: qsTr("Restore the previous layout")
                visible: root.workspaceLayout.hasBackup
                onClicked: root.workspaceLayout.restoreBackup()
            }
            Button {
                text: qsTr("Dismiss")
                onClicked: root.workspaceLayout.dismissNotice()
            }
        }
    }

    footer: ColumnLayout {
      spacing: 0
      // P6: the open Documents' tabs (legacy Notebook's bar).
      DocumentTabBar {
        app: root.app
        Layout.fillWidth: true
        onCloseRequested: index => root.closeTab(index)
      }
      RowLayout {
        // Legacy's first field: help and Automation status text (set_status_text),
        // never the editing target, which the Document tab and the window
        // title name.
        Label {
            objectName: "statusText"
            padding: 4
            text: shell.statusText
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            objectName: "selectionStatus"
            padding: 4
            text: shell.selectionStatus
        }
        Label {
            objectName: "saveStatus"
            padding: 4
            text: (root.editor.dirty ? qsTr("Modified") : "") + (root.editor.saveStatus.length
                  ? (root.editor.dirty ? "  |  " : "") + root.editor.saveStatus : "")
        }
      }
    }

    // Legacy HistoryDialog: every step, the current one selected; Set and a
    // double-click jump there and stay open, OK jumps and closes.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: historyWindow
        objectName: "historyWindow"
        // K1: the set's history icon as the window's (as legacy's dialogs show theirs, SetIcon).
        Component.onCompleted: IconTheme.setWindowIcon(historyWindow, "history")
        title: root.editor.history.length === 1 ? qsTr("History (1 element)")
                                                : qsTr("History (%1 elements)").arg(root.editor.history.length)
        width: 360
        height: 420
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        onVisibleChanged: if (visible) historyList.currentIndex = root.editor.historyCursor
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            ListView {
                id: historyList
                objectName: "historyList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: root.editor.history
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: historyWindow.title
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    font.bold: index === root.editor.historyCursor
                    onClicked: historyList.currentIndex = index
                    onDoubleClicked: root.editor.goToHistory(index)
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "historySet"
                    text: qsTr("Set")
                    onClicked: root.editor.goToHistory(historyList.currentIndex)
                }
                Button {
                    objectName: "historyOk"
                    text: qsTr("OK")
                    onClicked: { root.editor.goToHistory(historyList.currentIndex); historyWindow.close() }
                }
                Button {
                    objectName: "historyCancel"
                    text: qsTr("Cancel")
                    onClicked: historyWindow.close()
                }
            }
        }
    }

    // Legacy HikariSubFrame::Save: "Save subtitle file" for the Document's
    // format, starting at its file (or the video's, with the video name).
    function saveSubtitles() {
        const route = root.app.saveRoute()
        if (route === "dialog")
            openSaveDialog()
        else if (route === "readonly")
            readOnlyWarning.open()
        else
            root.editor.save()
    }
    function openSaveDialog() {
        const v = root.app.saveDialogValues()
        saveAsDialog.nameFilters = [v.filter]
        if (v.folder.toString() !== "")
            saveAsDialog.currentFolder = v.folder
        if (v.file.toString() !== "")
            saveAsDialog.selectedFile = v.file
        saveAsDialog.open()
    }
    FileDialog {
        id: saveAsDialog
        objectName: "saveAsDialog"
        title: qsTr("Save subtitle file")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Subtitle file ") + "(*.ass)"]
        onAccepted: {
            if (root.app.saveChosen(selectedFile) === "readonly")
                readOnlyWarning.open()
            else
                matroskaSubtitles.saveDone() // Y9: the load waits for the Save dialog
        }
        onRejected: matroskaSubtitles.saveDone()
    }
    // Y9: GRID_SUBS_FROM_MKV's question, track chooser and progress.
    MatroskaSubtitles {
        id: matroskaSubtitles
        matroska: root.matroska
        save: function() {
            const route = root.app.saveRoute()
            root.saveSubtitles()
            return route === "dialog" || route === "readonly"
        }
    }
    Dialog {
        id: readOnlyWarning
        objectName: "readOnlyWarning"
        title: qsTr("Warning")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { text: qsTr("Chosen file is read only,\nplease save with different name or change file attribute.") }
        onClosed: root.openSaveDialog()
    }

    // The accepted close review: every affected Document with Save or Discard,
    // Save all, Discard all and Cancel. Nothing closes until every save is
    // acknowledged as written.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: closeReview
        objectName: "closeReview"
        title: qsTr("Unsaved changes")
        width: 480
        height: 320
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var rows: []
        property var choices: []
        property string problem: ""
        // Closing the window is Cancel (a waiting file result is then shown).
        onClosing: root.app.cancelClose()
        function review(list) {
            choices = list.map(r => ({ id: r.id, save: true, path: "" }))
            rows = list
            problem = ""
            show()
        }
        function proceed() {
            for (let i = 0; i < rows.length; ++i)
                if (choices[i].save && rows[i].untitled && choices[i].path.length === 0) {
                    problem = qsTr("Choose where to save %1.").arg(rows[i].title)
                    return
                }
            root.app.resolveClose(choices)
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                text: qsTr("These Documents have unsaved changes:")
            }
            Repeater {
                model: closeReview.rows
                delegate: RowLayout {
                    id: rowItem
                    required property int index
                    required property var modelData
                    Label {
                        text: rowItem.modelData.title
                        Layout.fillWidth: true
                    }
                    RadioButton {
                        objectName: "closeSave" + rowItem.index
                        text: qsTr("Save")
                        checked: closeReview.choices[rowItem.index].save
                        onToggled: closeReview.choices[rowItem.index].save = checked
                    }
                    RadioButton {
                        objectName: "closeDiscard" + rowItem.index
                        text: qsTr("Discard")
                        checked: !closeReview.choices[rowItem.index].save
                        onToggled: closeReview.choices[rowItem.index].save = !checked
                    }
                    Button {
                        visible: rowItem.modelData.untitled
                        text: qsTr("Save as…")
                        onClicked: {
                            closeSaveAs.row = rowItem.index
                            closeSaveAs.open()
                        }
                    }
                }
            }
            Label {
                objectName: "closeReviewProblem"
                text: closeReview.problem
                visible: text.length > 0
                color: Theme.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "closeSaveAll"
                    text: qsTr("Save all")
                    onClicked: { closeReview.choices.forEach(c => c.save = true); closeReview.proceed() }
                }
                Button {
                    objectName: "closeDiscardAll"
                    text: qsTr("Discard all")
                    onClicked: { closeReview.choices.forEach(c => c.save = false); closeReview.proceed() }
                }
                Button {
                    objectName: "closeContinue"
                    text: qsTr("Continue")
                    onClicked: closeReview.proceed()
                }
                Button {
                    objectName: "closeCancel"
                    text: qsTr("Cancel")
                    onClicked: { root.app.cancelClose(); closeReview.close() }
                }
            }
        }
        FileDialog {
            id: closeSaveAs
            property int row: -1
            fileMode: FileDialog.SaveFile
            nameFilters: [qsTr("ASS subtitles (*.ass)"), qsTr("All files (*)")]
            onAccepted: closeReview.choices[row].path = root.app.localPath(selectedFile)
        }
    }

    // Automation windows (S1): the fixed script dialog and picker, the
    // manager tool and the progress window (legacy LuaProgressDialog).
    AutomationDialog {
        objectName: "automationDialog"
        controller: root.automationDialogs
    }
    AutomationFilePicker {
        picker: root.automationPicker
    }
    AutomationNotices {
        automation: root.automation
    }
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: automationManagerWindow
        objectName: "automationManagerWindow"
        // K1: the set's automation icon as the window's (as legacy's dialogs show theirs, SetIcon).
        Component.onCompleted: IconTheme.setWindowIcon(automationManagerWindow, "automation")
        title: qsTr("Automation manager")
        width: 560
        height: 420
        AutomationManager {
            anchors.fill: parent
            controller: root.automationManager
        }
    }
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: automationProgress
        objectName: "automationProgress"
        title: root.automation.runTitle
        width: 520
        height: 340
        flags: Qt.Dialog
        // Shown while a macro runs; it stays after a failure so its log can be read.
        property bool keep: false
        visible: root.automation.running || keep
        Connections {
            target: root.automation
            function onRunCompleted(ok, message) { automationProgress.keep = !ok && message.length > 0 }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                objectName: "automationTask"
                text: root.automation.task
                Layout.fillWidth: true
            }
            ProgressBar {
                from: 0
                to: 100
                value: root.automation.progress
                Layout.fillWidth: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    objectName: "automationLog"
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    text: root.automation.log
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "automationForceStop"
                    text: qsTr("Force stop")
                    visible: root.automation.forceStopOffered
                    onClicked: root.automation.forceStopRun()
                }
                Button {
                    objectName: "automationCancel"
                    text: root.automation.running ? qsTr("Cancel") : qsTr("Close")
                    onClicked: {
                        if (root.automation.running)
                            root.automation.cancelRun()
                        else
                            automationProgress.keep = false
                    }
                }
            }
        }
    }
    FileDialog {
        id: scriptDialog
        nameFilters: [qsTr("Automation scripts (*.lua *.moon)"), qsTr("All files (*)")]
        onAccepted: root.automation.loadScript(selectedFile)
    }

    FileDialog {
        id: videoDialog
        nameFilters: [qsTr("Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"), qsTr("All files (*)")]
        onAccepted: root.video.openVideoUrl(selectedFile)
    }

    FileDialog {
        id: openDialog
        nameFilters: [qsTr("Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"), qsTr("All files (*)")]
        onAccepted: root.openSubtitles(root.app.localPath(selectedFile))
    }

    // GRID_SET_NEW_FPS (legacy FPSDialog): the subtitles' FPS and the new one.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: fpsWindow
        objectName: "fpsWindow"
        title: qsTr("Choose new FPS")
        width: 360
        height: 140
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        readonly property var presets: ["23.976", "24", "25", "29.97", "30", "60"]
        GridLayout {
            anchors.fill: parent
            anchors.margins: 8
            columns: 2
            Label { text: qsTr("Subtitles FPS") }
            ComboBox {
                id: oldFps
                objectName: "oldFps"
                editable: true
                model: fpsWindow.presets
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("Subtitles FPS")
            }
            Label { text: qsTr("New FPS") }
            ComboBox {
                id: newFps
                objectName: "newFps"
                editable: true
                model: fpsWindow.presets
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("New FPS")
            }
            RowLayout {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "fpsOk"
                    text: qsTr("OK")
                    onClicked: {
                        if (root.app.setNewFps(oldFps.editText, newFps.editText))
                            fpsWindow.close()
                    }
                }
                Button {
                    objectName: "fpsCancel"
                    text: qsTr("Cancel")
                    onClicked: fpsWindow.close()
                }
            }
        }
    }

    // The column choice for Copy columns / Paste columns (legacy Stylelistbox),
    // checked as last chosen.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: columnsWindow
        objectName: "columnsWindow"
        title: paste ? qsTr("Paste columns") : qsTr("Copy columns")
        width: 320
        height: 420
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property bool paste: false
        property var rows: []
        function choose(forPaste) {
            paste = forPaste
            rows = root.app.columnChoices(forPaste)
            show()
        }
        function chosen() {
            let bits = 0
            for (let i = 0; i < columnRepeater.count; ++i)
                if (columnRepeater.itemAt(i).checked)
                    bits |= rows[i].bit
            return bits
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Repeater {
                id: columnRepeater
                model: columnsWindow.rows
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "column" + index
                    text: modelData.label
                    checked: modelData.checked
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "columnsOk"
                    text: qsTr("OK")
                    onClicked: {
                        const bits = columnsWindow.chosen()
                        columnsWindow.close()
                        if (columnsWindow.paste)
                            root.app.pasteColumns(bits)
                        else
                            root.app.copyColumns(bits)
                    }
                }
                Button {
                    objectName: "columnsCancel"
                    text: qsTr("Cancel")
                    onClicked: columnsWindow.close()
                }
            }
        }
    }

    // Legacy LogWindow: a message pops it up with just that message; the File
    // menu entry shows the whole log.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: logWindow
        objectName: "logWindow"
        title: qsTr("Log window")
        width: 520
        height: root.log.full ? 380 : 140
        flags: Qt.Dialog
        visible: root.log.shown
        onClosing: root.log.close()
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                objectName: "logLastMessage"
                visible: !root.log.full
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: root.log.lastMessage
            }
            ScrollView {
                visible: root.log.full
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    objectName: "logHistory"
                    readOnly: true
                    text: root.log.history
                    Accessible.name: qsTr("Log")
                }
            }
            Item { Layout.fillHeight: !root.log.full }
            Button {
                objectName: "logClose"
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Close")
                onClicked: root.log.close()
            }
        }
    }

    // D1: every drag placement from the keyboard, and numeric resizing.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: placementWindow
        objectName: "placementWindow"
        title: qsTr("Move panel")
        width: 420
        height: 260
        transientParent: root
        flags: Qt.Dialog
        function openFor(dock) {
            placementPanel.currentIndex = Math.max(0, root.dockList.indexOf(dock))
            refreshSize()
            show()
            requestActivate()
            placementPanel.forceActiveFocus()
        }
        function refreshSize() {
            const size = root.workspaceLayout.panelSize(root.dockList[placementPanel.currentIndex])
            placementWidth.value = size.width
            placementHeight.value = size.height
        }
        GridLayout {
            anchors.fill: parent
            anchors.margins: 8
            columns: 2
            Label { text: qsTr("Panel:") }
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
                Accessible.name: qsTr("Panel")
                onActivated: placementWindow.refreshSize()
            }
            Label { text: qsTr("Place:") }
            ComboBox {
                id: placementKind
                objectName: "placementKind"
                Layout.fillWidth: true
                model: [qsTr("Tab with"), qsTr("Left of"), qsTr("Above"), qsTr("Right of"), qsTr("Below"), qsTr("Float")]
                Accessible.name: qsTr("Placement")
            }
            Label { text: qsTr("Next to:") }
            ComboBox {
                id: placementTarget
                objectName: "placementTarget"
                Layout.fillWidth: true
                enabled: placementKind.currentIndex !== 5
                model: root.dockList
                textRole: "title"
                displayText: placementWindow.visible && currentIndex >= 0 ? root.dockList[currentIndex].title : ""
                currentIndex: 3 // the Grid
                Accessible.name: qsTr("Next to panel")
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "placementMove"
                text: qsTr("Move")
                onClicked: {
                    root.placePanel(root.dockList[placementPanel.currentIndex], placementKind.currentIndex,
                                    root.dockList[placementTarget.currentIndex])
                    placementWindow.refreshSize()
                }
            }
            Label { text: qsTr("Width:") }
            SpinBox {
                id: placementWidth
                objectName: "placementWidth"
                from: 0; to: 10000; stepSize: 10; editable: true
                Accessible.name: qsTr("Width")
            }
            Label { text: qsTr("Height:") }
            SpinBox {
                id: placementHeight
                objectName: "placementHeight"
                from: 0; to: 10000; stepSize: 10; editable: true
                Accessible.name: qsTr("Height")
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "placementResize"
                text: qsTr("Resize")
                onClicked: {
                    root.workspaceLayout.resizePanel(root.dockList[placementPanel.currentIndex],
                                                     placementWidth.value, placementHeight.value)
                    placementWindow.refreshSize()
                }
            }
        }
    }

    // A1: legacy OpenAudioInTab's dialog and filter.
    FileDialog {
        id: audioDialog
        objectName: "audioDialog"
        title: qsTr("Choose audio file")
        nameFilters: [qsTr("Audio and video files") + " (*.wav *.w64 *.flac *.ac3 *.aac *.ogg *.mp3 *.mp4 *.m4a *.mkv *.avi)",
                      qsTr("All files") + " (*)"]
        onAccepted: root.audio.openAudioUrl(selectedFile)
    }
    // A1: legacy HikariListBox "Choose the track" (ProviderFFMS2::Init with
    // several audio tracks): the first row preselected, OK or a double click
    // takes a row, Cancel opens no audio.
    Dialog {
        id: audioTrackChooser
        objectName: "audioTrackChooser"
        title: qsTr("Choose the track")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ListView {
            id: audioTrackList
            objectName: "audioTrackList"
            implicitWidth: 220
            implicitHeight: 160
            clip: true
            model: root.audio.trackChoices
            currentIndex: 0
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: audioTrackList.currentIndex = index
                onDoubleClicked: audioTrackChooser.accept()
            }
        }
        onAccepted: root.audio.chooseTrack(Math.max(0, audioTrackList.currentIndex))
        onRejected: root.audio.cancelTrackChoice()
        Connections {
            target: root.audio
            function onTrackChoicesChanged() {
                if (root.audio.trackChoices.length > 0) {
                    audioTrackList.currentIndex = 0
                    audioTrackChooser.open()
                } else if (audioTrackChooser.opened) {
                    audioTrackChooser.close()
                }
            }
        }
    }
    FileDialog {
        id: keyframesDialog
        title: qsTr("Choose video file")
        nameFilters: [qsTr("Keyframes file (*.txt *.pass *.stats *.log)"), qsTr("All files (*)")]
        onAccepted: {
            const problem = root.app.openKeyframes(selectedFile)
            if (problem.length > 0)
                root.log.log(problem)
        }
    }
    FileDialog {
        id: translationFileDialog
        title: qsTr("Choose subtitle file")
        nameFilters: [qsTr("Subtitle files (*.ass *.srt *.sub *.txt)")]
        onAccepted: root.app.pasteTranslationFile(selectedFile)
    }
    // Legacy TLDialog: moves the translation or the original against the
    // other from the first selected Line; it stays open while working.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: translationShiftWindow
        objectName: "translationShiftWindow"
        title: qsTr("Translation matching options")
        width: 360
        height: 250
        transientParent: root
        flags: Qt.Dialog
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            RowLayout {
                Label { text: qsTr("Original"); Layout.fillWidth: true }
                Label { text: qsTr("Translation"); Layout.fillWidth: true }
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                Repeater {
                    // Legacy layout: Original left, Translation right; mode is MoveTextTL's.
                    model: [
                        { mode: 2, name: qsTr("Add line"), tip: qsTr("Adds a blank line before the selected line.\nMoves the original one line down.\nThe added line must be timed.") },
                        { mode: 3, name: qsTr("Add line"), tip: qsTr("Adds a blank line before the selected line.\nMoves the translation one line down.") },
                        { mode: 4, name: qsTr("Join lines"), tip: qsTr("Joins the selected line with the next line.\nMoves the original one line up.") },
                        { mode: 1, name: qsTr("Join lines"), tip: qsTr("Joins the selected line with the next line.\nMoves the translation one line up.") },
                        { mode: 5, name: qsTr("Delete line"), tip: qsTr("Deletes the selected line.\nMoves the original one line up.") },
                        { mode: 0, name: qsTr("Delete line"), tip: qsTr("Deletes the selected line.\nMoves the translation one line up.") }
                    ]
                    Button {
                        required property var modelData
                        objectName: "translationMove" + modelData.mode
                        text: modelData.name
                        Layout.fillWidth: true
                        Accessible.description: modelData.tip
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tip
                        onClicked: root.app.shiftTranslation(modelData.mode)
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Description:\nOriginal - subtitle text with correct timing, used to compare pasted dialogue lines; it is deleted later.\nTranslation - text pasted into subtitles with correct timing.")
            }
        }
    }
    Dialog {
        id: shiftConfirm
        objectName: "shiftConfirm"
        property int which: 1
        title: qsTr("Confirmation")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Do you really want to shift only %1 times?").arg(shiftConfirm.which === 1 ? qsTr("start") : qsTr("end")) }
        onAccepted: shiftMessage.text = root.app.shiftTimes()
    }
    // P8: legacy UpdateChecker. Only the menu's check reports "up to date"
    // and failures; the automatic one speaks only for a newer release.
    Connections {
        target: root.updates
        function onFinished(outcome, release, interactive) {
            if (outcome === "available") {
                updateAvailable.release = release
                updateAvailable.open()
            } else {
                updateMessage.text = outcome === "current" ? qsTr("You already have the latest version")
                                                          : qsTr("Cannot check for updates")
                updateMessage.open()
            }
        }
    }
    Dialog {
        id: updateMessage
        objectName: "updateMessage"
        property alias text: updateMessageLabel.text
        title: qsTr("Update")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: updateMessageLabel }
    }
    Dialog {
        id: updateAvailable
        objectName: "updateAvailable"
        property var release: ({})
        title: qsTr("A new version is available")
        modal: true
        anchors.centerIn: parent
        ColumnLayout {
            anchors.fill: parent
            Label { text: updateAvailable.release.name || ""; font.bold: true }
            Label { text: qsTr("You have version %1; version %2 is available").arg(root.updates.version).arg(updateAvailable.release.tag || "") }
            ScrollView {
                Layout.preferredWidth: 460
                Layout.preferredHeight: 220
                TextArea { objectName: "updateNotes"; text: updateAvailable.release.notes || ""; readOnly: true; wrapMode: TextEdit.Wrap }
            }
            Label { text: updateAvailable.release.url || "" }
            CheckBox {
                objectName: "updateAutoCheck"
                text: qsTr("Check for updates automatically")
                checked: root.updates.autoCheck
                onToggled: root.updates.autoCheck = checked
            }
            CheckBox {
                objectName: "updateStableOnly"
                text: qsTr("Stable versions only")
                checked: root.updates.stableOnly
                onToggled: root.updates.stableOnly = checked
            }
            RowLayout {
                Button { text: qsTr("Open the download page"); onClicked: root.updates.openReleasePage(updateAvailable.release.url || "") }
                Button { objectName: "updateRemind"; text: qsTr("Remind me in a week"); onClicked: { root.updates.remindInAWeek(); updateAvailable.close() } }
                Button { text: qsTr("Close"); onClicked: updateAvailable.close() }
            }
        }
    }
    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        title: qsTr("About HikariSub")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label {
            objectName: "aboutText"
            text: qsTr("HikariSub subtitle editor by altqx,\nversion %1").arg(root.updates.version) + " \n\n" +
                  qsTr("Based on Kainote by Marcin Drob aka Bakura or Bjakja.\n\n") +
                  qsTr("If you have noticed any bugs or have any suggestions for changes or new features,\nopen an issue at https://github.com/altqx/hikari/issues.\n\n") +
                  qsTr("HikariSub includes parts of the following projects:\n") +
                  "Qt - Copyright © The Qt Company Ltd. and other contributors.\n" +
                  "KDDockWidgets - Copyright © Klarälvdalens Datakonsult AB (KDAB).\n" +
                  qsTr("Color picker, audio box, audio player, automation,\nand several other individual features taken from Aegisub -\n") +
                  "Copyright © Rodrigo Braz Monteiro.\n" +
                  "Hunspell - Copyright © Kevin Hendricks.\n" +
                  "FFMPEGSource2 - Copyright © Fredrik Mellbin.\n" +
                  "FFmpeg - Copyright © the FFmpeg developers.\n" +
                  "LuaJIT - Copyright © Mike Pall.\n" +
                  "ICU - Copyright © 1995-2016 International Business Machines Corporation and others.\n" +
                  "Boost - Copyright © Joe Coder 2004 - 2006.\n" +
                  "FreeType2 - Copyright © 2006-2019 David Turner, Robert Wilhelm, and Werner Lemberg.\n" +
                  "Fribidi - Copyright © 1991, 1999 Free Software Foundation, Inc.\n" +
                  "Libass - Copyright © 2006-2016 libass contributors.\n"
        }
    }
    Dialog {
        id: creditsDialog
        objectName: "creditsDialog"
        title: qsTr("Credits")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label {
            text: qsTr("Graphical help: (buttons, help pictures etc.)\n") +
                  qsTr("- Xandros (new video buttons).\n") +
                  qsTr("- Devilkan (menu and toolbar buttons)\n") +
                  qsTr("- Zły Los (icons for file associations and menu).\n") +
                  qsTr("Testers:\n") +
                  qsTr("- Sacredus (first translator using translator mode,\ngreat help testing HikariSub on a slow computer)\n") +
                  qsTr("- Devilkan (crashhunter, because of his system and habits he made HikariSub crash a lot\n") +
                  qsTr("helped with typesetting tools and came up with many more improvements).\n") +
                  qsTr("- MatiasMovie (found some crashes and suggessted some improvements, helps with crash debugging).\n") +
                  qsTr("- mas1904 (found some errors and helping with crash debuging).\n") +
                  qsTr("- Senami (made new themes and found some bugs).\n") +
                  qsTr("- altinat (Thai translation).\n") +
                  qsTr("- labrie75 (Korean translation).\n") +
                  qsTr("- Niskala5570 (Malay translation).\n") +
                  qsTr("Thanks to other HikariSub users who reported bugs:\n") +
                  "SoheiMajin, BadRequest, Ognisty321, ZlyLos, Thomas Leigh, TomBit."
        }
    }
    // Y5: conversion with the approved loss preview (C02-loss-preview): the
    // legacy CONVERT_* options, then what the conversion removes or
    // synthesizes; Convert applies exactly the previewed plan.
    Dialog {
        id: conversionDialog
        objectName: "conversionDialog"
        modal: true
        anchors.centerIn: parent
        property string target: ""
        property var preview: ({})
        function openFor(t, label) {
            target = t
            title = label
            const o = root.app.conversionOptions()
            convFps.text = o.fps
            convFpsFromVideo.checked = o.fpsFromVideo
            convStyle.text = o.style
            convCatalog.currentIndex = Math.max(0, root.styleManager.catalogs.indexOf(o.styleCatalog))
            convNewEnds.checked = o.newEndTimes
            convPerLetter.value = o.timePerCharacter
            convPrefix.text = o.prefix
            convWidth.text = o.resolutionWidth
            convHeight.text = o.resolutionHeight
            refresh()
            open()
        }
        function refresh() { preview = root.app.previewConversion(target) }
        function set(key, value) { root.app.setConversionOptions({[key]: value}); refresh() }
        ColumnLayout {
            anchors.fill: parent
            GridLayout {
                columns: 2
                Label { text: qsTr("FPS") }
                RowLayout {
                    TextField { id: convFps; objectName: "convFps"; Accessible.name: qsTr("FPS"); onEditingFinished: conversionDialog.set("fps", text) }
                    CheckBox { id: convFpsFromVideo; text: qsTr("FPS from video"); onToggled: conversionDialog.set("fpsFromVideo", checked) }
                }
                Label { text: qsTr("Catalog for style") }
                ComboBox {
                    id: convCatalog
                    model: root.styleManager.catalogs
                    Accessible.name: qsTr("Catalog for style")
                    onActivated: (i) => conversionDialog.set("styleCatalog", model[i])
                }
                Label { text: qsTr("Style") }
                TextField { id: convStyle; Accessible.name: qsTr("Style"); onEditingFinished: conversionDialog.set("style", text) }
                Label { text: qsTr("Time for one letter in milliseconds") }
                RowLayout {
                    SpinBox { id: convPerLetter; from: 30; to: 1000; editable: true; onValueModified: conversionDialog.set("timePerCharacter", value) }
                    CheckBox { id: convNewEnds; objectName: "convNewEnds"; text: qsTr("New end times"); onToggled: conversionDialog.set("newEndTimes", checked) }
                }
                Label { text: qsTr("Tags to paste at the beginning of every ASS line") }
                TextField { id: convPrefix; Accessible.name: qsTr("Tags to paste at the beginning of every ASS line"); onEditingFinished: conversionDialog.set("prefix", text) }
                Label { text: qsTr("Resolution when converting to ASS") }
                RowLayout {
                    TextField { id: convWidth; Accessible.name: qsTr("Width"); onEditingFinished: conversionDialog.set("resolutionWidth", text) }
                    Label { text: " x " }
                    TextField { id: convHeight; Accessible.name: qsTr("Height"); onEditingFinished: conversionDialog.set("resolutionHeight", text) }
                }
            }
            Label {
                text: conversionDialog.preview.ok ? qsTr("The conversion will:") : (conversionDialog.preview.problem || "")
                font.bold: true
            }
            ListView {
                objectName: "conversionLosses"
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                Layout.preferredWidth: 480
                clip: true
                model: conversionDialog.preview.ok ? conversionDialog.preview.losses : []
                Accessible.role: Accessible.List
                Accessible.name: qsTr("What the conversion changes")
                delegate: Label { required property string modelData; text: "• " + modelData; Accessible.name: modelData }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "conversionAccept"
                    text: qsTr("Convert")
                    enabled: conversionDialog.preview.ok === true
                    onClicked: {
                        if (!root.app.acceptConversion())
                            conversionDialog.refresh() // stale: show the new preview
                        else
                            conversionDialog.close()
                    }
                }
                Button { text: qsTr("Cancel"); onClicked: conversionDialog.close() }
            }
        }
    }
    // Y4: legacy SubsResampleDialog ("Change resolution").
    Dialog {
        id: resampleDialog
        objectName: "resampleDialog"
        title: qsTr("Change resolution")
        modal: true
        anchors.centerIn: parent
        property var initial: ({})
        function openDialog() {
            const v = root.app.resampleValues()
            if (v.subsWidth === undefined)
                return
            initial = v
            subsWidth.value = v.subsWidth
            subsHeight.value = v.subsHeight
            targetWidth.value = v.videoWidth
            targetHeight.value = v.videoHeight
            noStretch.checked = true
            open()
        }
        // The stretch choice only means something when the aspect ratio changes.
        readonly property bool aspectChanges: targetWidth.value / subsWidth.value !== targetHeight.value / subsHeight.value
        ColumnLayout {
            anchors.fill: parent
            GroupBox {
                title: qsTr("Subtitles resolution")
                Layout.fillWidth: true
                RowLayout {
                    SpinBox { id: subsWidth; objectName: "resampleSubsWidth"; from: 100; to: 13000; editable: true; Accessible.name: qsTr("Subtitles resolution") }
                    Label { text: " x " }
                    SpinBox { id: subsHeight; objectName: "resampleSubsHeight"; from: 100; to: 10000; editable: true }
                    Button {
                        text: qsTr("From subtitles")
                        enabled: subsWidth.value !== resampleDialog.initial.subsWidth || subsHeight.value !== resampleDialog.initial.subsHeight
                        onClicked: { subsWidth.value = resampleDialog.initial.subsWidth; subsHeight.value = resampleDialog.initial.subsHeight }
                    }
                }
            }
            GroupBox {
                title: qsTr("Target resolution")
                Layout.fillWidth: true
                RowLayout {
                    SpinBox { id: targetWidth; objectName: "resampleWidth"; from: 100; to: 13000; editable: true; Accessible.name: qsTr("Target resolution") }
                    Label { text: " x " }
                    SpinBox { id: targetHeight; objectName: "resampleHeight"; from: 100; to: 10000; editable: true }
                    Button {
                        text: qsTr("Get from video")
                        // Legacy compares the target with the subtitles' size here.
                        enabled: targetWidth.value !== resampleDialog.initial.subsWidth || targetHeight.value !== resampleDialog.initial.subsHeight
                        onClicked: { targetWidth.value = resampleDialog.initial.videoWidth; targetHeight.value = resampleDialog.initial.videoHeight }
                    }
                }
            }
            GroupBox {
                title: qsTr("Resample options")
                Layout.fillWidth: true
                enabled: resampleDialog.aspectChanges
                RowLayout {
                    RadioButton { id: noStretch; text: qsTr("No stretch"); checked: true }
                    RadioButton { id: stretch; objectName: "resampleStretch"; text: qsTr("Stretch") }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "resampleOk"
                    text: "OK"
                    onClicked: {
                        if (subsWidth.value === targetWidth.value && subsHeight.value === targetHeight.value)
                            return
                        root.app.resample(subsWidth.value, subsHeight.value, targetWidth.value, targetHeight.value,
                                          resampleDialog.aspectChanges && stretch.checked)
                        resampleDialog.close()
                    }
                }
                Button { text: qsTr("Cancel"); onClicked: resampleDialog.close() }
            }
        }
    }
    // Y4: legacy SubsMismatchResolutionDialog ("Incompatible resolution").
    Connections {
        target: root.app
        function onResolutionMismatch(sizes) { mismatchDialog.show(sizes) }
    }
    Dialog {
        id: mismatchDialog
        objectName: "mismatchDialog"
        title: qsTr("Incompatible resolution")
        modal: true
        anchors.centerIn: parent
        property var sizes: ({})
        readonly property bool canStretch: sizes.videoWidth / sizes.subsWidth !== sizes.videoHeight / sizes.subsHeight
        function show(s) {
            sizes = s
            mismatchResample.checked = true
            open()
        }
        ColumnLayout {
            anchors.fill: parent
            Label {
                objectName: "mismatchText"
                text: qsTr("The video and subtitle resolutions are different.\nYou can change them now or use 'Change subtitle resolution'.\n\nVideo resolution: %1 x %2\nSubtitle resolution: %3 x %4\n\nMatch the resolution to the video?\n")
                      .arg(mismatchDialog.sizes.videoWidth).arg(mismatchDialog.sizes.videoHeight)
                      .arg(mismatchDialog.sizes.subsWidth).arg(mismatchDialog.sizes.subsHeight)
            }
            GroupBox {
                title: qsTr("Resample options")
                ColumnLayout {
                    RadioButton { id: mismatchOnly; objectName: "mismatchOnly"; text: qsTr("Change only the subtitle resolution") }
                    RadioButton { id: mismatchResample; objectName: "mismatchResample"; text: qsTr("Resample subtitles (no stretch)"); checked: true }
                    RadioButton { id: mismatchStretch; objectName: "mismatchStretch"; text: qsTr("Resample subtitles (stretch)"); visible: mismatchDialog.canStretch }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "mismatchChange"
                    text: qsTr("Change")
                    onClicked: {
                        root.app.matchVideoResolution(mismatchOnly.checked ? 0 : mismatchStretch.checked ? 2 : 1)
                        mismatchDialog.close()
                    }
                }
                Button { text: qsTr("Do not change"); onClicked: mismatchDialog.close() }
                Button {
                    objectName: "mismatchDisable"
                    text: qsTr("Disable warning")
                    onClicked: { root.app.askForBadResolution = false; mismatchDialog.close() }
                }
            }
        }
    }
    StyleManager {
        id: styleManagerWindow
        styles: root.styleManager
        catalogs: root.fontCatalogs
        app: root.app
    }
    SettingsImportDialog {
        id: settingsImportDialog
        importer: root.settingsImport
        anchors.centerIn: parent
    }
    SettingsDialog {
        id: settingsDialog
        app: root.app
        hotkeys: root.hotkeys
        anchors.centerIn: parent
    }
    // Y8: the font collector; a double click on a Style in its log opens the
    // Style editor (StyleStore::ShowStyleEdit).
    FontCollectorDialog {
        id: fontCollectorDialog
        collector: root.fontCollector
        anchors.centerIn: parent
        Connections {
            target: root.fontCollector
            function onStyleRequested(style) {
                styleManagerWindow.showFor(style)
                const row = styleManagerWindow.styles.documentStyles.indexOf(style)
                if (row >= 0)
                    styleManagerWindow.beginEditing(styleManagerWindow.styles.beginEdit(false, row), false)
            }
        }
    }
    SelectLinesDialog {
        id: selectLinesDialog
        app: root.app
        anchors.centerIn: parent
        // DestroyDialogs (a changed program font): SaveOptions, then gone.
        Connections {
            target: root.app
            function onSelectLinesDestroyed() {
                if (selectLinesDialog.visible)
                    selectLinesDialog.close()
            }
        }
    }
    MisspellReplacerDialog {
        id: misspellDialog
        app: root.app
        // The main window's client origin is below its menu bar (legacy's
        // native menu bar was outside the client area).
        clientTop: root.menuBar ? root.menuBar.height : 0
        // DestroyDialogs (a changed program font): MR->Destroy().
        Connections {
            target: root.app
            function onMisspellReplacerDestroyed() { misspellDialog.destroyDialog() }
        }
    }
    // F3: the Spellchecker window, legacy's spelling message boxes and the
    // editor's "Fix suggestions" list.
    SpellCheckerDialog {
        id: spellCheckerDialog
        app: root.app
        anchors.centerIn: parent
    }
    Dialog {
        id: spellingNotice
        objectName: "spellingNotice"
        property alias text: spellingNoticeLabel.text
        anchors.centerIn: parent
        modal: true // legacy HikariMessageBox is modal
        standardButtons: Dialog.Ok
        Label { id: spellingNoticeLabel; Accessible.role: Accessible.AlertMessage }
    }
    Connections {
        target: root.app
        function onSpellingNotice(message) {
            spellingNotice.text = message
            spellingNotice.open()
        }
    }
    Dialog {
        id: fixSuggestions
        objectName: "fixSuggestions"
        title: qsTr("Fix suggestions")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int role: 0
        property int position: -1
        property var suggestions: []
        function openFor(role, position, suggestions) {
            fixSuggestions.role = role
            fixSuggestions.position = position
            fixSuggestions.suggestions = suggestions
            fixList.currentIndex = 0
            open()
        }
        onAccepted: if (fixList.currentIndex >= 0 && fixList.currentIndex < suggestions.length)
            root.app.replaceEditorMisspell(role, position, suggestions[fixList.currentIndex])
        ListView {
            id: fixList
            objectName: "fixSuggestionsList"
            implicitWidth: 220
            implicitHeight: 160
            clip: true
            model: fixSuggestions.suggestions
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: fixList.currentIndex = index
                onDoubleClicked: {
                    fixList.currentIndex = index
                    fixSuggestions.accept()
                }
            }
        }
    }
    ScriptPropertiesDialog {
        id: scriptPropertiesDialog
        app: root.app
        anchors.centerIn: parent
    }
    FontDialog {
        id: fontDialog
        editor: root.editor
        catalogs: root.fontCatalogs
        anchors.centerIn: parent
    }
    ColourPickerDialog {
        id: colourDialog
        editor: root.editor
        picker: root.colourPicker
        anchors.centerIn: parent
    }

    // Legacy TagButtonDialog ("Enter ASS tag").
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: tagButtonDialog
        objectName: "tagButtonDialog"
        title: qsTr("Enter ASS tag")
        width: 320
        height: 240
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property int index: -1
        function editButton(i) {
            const b = root.tagButtons.buttons[i]
            index = i
            tagType.currentIndex = b.type
            tagName.text = b.name
            tagText.text = b.tag
            show()
            tagText.selectAll()
            tagText.forceActiveFocus()
        }
        function saveTag() {
            root.tagButtons.edit(index, tagName.text, tagText.text, tagType.currentIndex)
            close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            ComboBox {
                id: tagType
                objectName: "tagType"
                Layout.fillWidth: true
                model: [qsTr("Tag inserted in place of cursor"), qsTr("Insert Tag at text beginning"), qsTr("Plain text")]
                Accessible.name: qsTr("Insertion")
            }
            Label { text: qsTr("Button name") }
            TextField {
                id: tagName
                objectName: "tagName"
                Layout.fillWidth: true
                Accessible.name: qsTr("Button name")
                onAccepted: tagButtonDialog.saveTag()
            }
            Label { text: qsTr("Button tag") }
            TextField {
                id: tagText
                objectName: "tagText"
                Layout.fillWidth: true
                Accessible.name: qsTr("Button tag")
                onAccepted: tagButtonDialog.saveTag()
            }
            RowLayout {
                Button {
                    objectName: "saveTag"
                    text: qsTr("Save tag")
                    onClicked: tagButtonDialog.saveTag()
                }
                Button {
                    objectName: "cancelTag"
                    text: qsTr("Cancel")
                    onClicked: tagButtonDialog.close()
                }
            }
        }
    }

    // Legacy NumTagButtons ("Change number of buttons", 0 to 20).
    Dialog {
        id: tagButtonCountDialog
        objectName: "tagButtonCountDialog"
        title: qsTr("Change number of buttons")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: tagButtonCount.value = root.tagButtons.storedCount()
        onAccepted: root.tagButtons.setCount(tagButtonCount.value)
        SpinBox {
            id: tagButtonCount
            objectName: "tagButtonCount"
            from: 0
            to: 20
            editable: true
            Accessible.name: qsTr("Number of buttons")
        }
    }

    // Legacy TreeDialog ("Tree description").
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: groupDescriptionDialog
        objectName: "groupDescriptionDialog"
        title: qsTr("Tree description")
        width: 440
        height: 120
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var description: 0
        function edit(id) {
            description = id
            groupDescription.text = root.app.groupTitle(id)
            show()
            groupDescription.selectAll()
            groupDescription.forceActiveFocus()
        }
        function accept() {
            if (groupDescription.text.length > 0 && root.app.renameGroup(description, groupDescription.text))
                close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            TextField {
                id: groupDescription
                objectName: "groupDescription"
                Layout.fillWidth: true
                maximumLength: 500
                Accessible.name: qsTr("Tree description")
                onAccepted: groupDescriptionDialog.accept()
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "groupDescriptionOk"
                    text: root.app.groupTitle(groupDescriptionDialog.description).length === 0 ? qsTr("Set tree name") : qsTr("Change tree name")
                    onClicked: groupDescriptionDialog.accept()
                }
                Button {
                    objectName: "groupDescriptionCancel"
                    text: qsTr("Cancel")
                    onClicked: groupDescriptionDialog.close()
                }
            }
        }
    }

    // G56-contiguity: a command that would break a Line group is refused;
    // Cancel, or remove the group (its description Line is deleted) and retry.
    Connections {
        target: root.app
        function onGroupBreakRefused(description, title) {
            groupBreakDialog.description = description
            groupBreakDialog.title2 = title
            groupBreakDialog.show()
        }
    }
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: groupBreakDialog
        objectName: "groupBreakDialog"
        title: qsTr("Line group")
        width: 460
        height: 150
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var description: 0
        property string title2: ""
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: groupBreakDialog.description
                      ? qsTr("This would break the Line group “%1”. Nothing was changed. Remove the group (its description Line is deleted) and try again, or cancel.").arg(groupBreakDialog.title2)
                      : qsTr("This would leave group members outside a Line group. Nothing was changed.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "groupBreakRemove"
                    visible: groupBreakDialog.description
                    text: qsTr("Remove group")
                    onClicked: {
                        root.app.removeGroup(groupBreakDialog.description)
                        groupBreakDialog.close()
                    }
                }
                Button {
                    objectName: "groupBreakCancel"
                    text: qsTr("Cancel")
                    onClicked: groupBreakDialog.close()
                }
            }
        }
    }

    // P3: work left by a session that did not close cleanly. Each bundle opens
    // as a new unsaved copy (L58-recovery-copy) or is dismissed.
    Component.onCompleted: {
        if (root.app.recoveryBundles().length > 0)
            recoveryWindow.showBundles()
    }
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: recoveryWindow
        objectName: "recoveryWindow"
        title: qsTr("Open auto save")
        width: 560
        height: 360
        flags: Qt.Dialog
        property var bundles: []
        function showBundles() {
            bundles = root.app.recoveryBundles()
            show()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: recoveryWindow.bundles.length > 0
                      ? qsTr("Unsaved work from a session that did not close. Each opens as a new unsaved copy; the original file is not changed.")
                      : qsTr("There is no unsaved work to recover.")
            }
            ListView {
                objectName: "recoveryList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: recoveryWindow.bundles
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: qsTr("%1 — %2").arg(modelData.title).arg(modelData.written)
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.original
                    }
                    ComboBox {
                        id: generationChoice
                        objectName: "recoveryGeneration" + index
                        model: modelData.generations.map(g => g.written)
                        Accessible.name: qsTr("Autosave")
                    }
                    Button {
                        objectName: "recoverBundle" + index
                        text: qsTr("Open copy")
                        onClicked: {
                            if (root.app.recoverBundle(modelData.key, modelData.generations[generationChoice.currentIndex].generation))
                                recoveryWindow.close()
                        }
                    }
                    Button {
                        objectName: "dismissBundle" + index
                        text: qsTr("Dismiss")
                        onClicked: {
                            root.app.dismissBundle(modelData.key)
                            recoveryWindow.bundles = root.app.recoveryBundles()
                        }
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                objectName: "recoveryClose"
                text: qsTr("Close")
                onClicked: recoveryWindow.close()
            }
        }
    }

    // P4: legacy AutoSavesRemoving for the autosaves this application keeps
    // (no index or audio caches are written to disk).
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: temporaryFilesWindow
        objectName: "temporaryFilesWindow"
        title: qsTr("Remove temporary files")
        width: 520
        height: 380
        flags: Qt.Dialog
        property var bundles: []
        property var checked: ({})
        function showFiles() {
            bundles = root.app.recoveryBundles()
            checked = ({})
            // Legacy default: a month before today.
            const before = new Date()
            before.setMonth(before.getMonth() - 1)
            olderDay.currentIndex = before.getDate() - 1
            olderMonth.currentIndex = before.getMonth()
            olderYear.text = before.getFullYear()
            show()
        }
        function cutoff() {
            return new Date(parseInt(olderYear.text), olderMonth.currentIndex, olderDay.currentIndex + 1)
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label { text: qsTr("Auto save") }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: temporaryFilesWindow.bundles
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "autosave" + index
                    text: qsTr("%1 — %2").arg(modelData.title).arg(modelData.written)
                    onToggled: temporaryFilesWindow.checked[modelData.key] = checked
                }
            }
            RowLayout {
                Button {
                    objectName: "removeSelectedAutosaves"
                    text: qsTr("Remove selected autosave files")
                    onClicked: {
                        for (const key in temporaryFilesWindow.checked)
                            if (temporaryFilesWindow.checked[key])
                                root.app.dismissBundle(key)
                        temporaryFilesWindow.showFiles()
                    }
                }
                Button {
                    objectName: "removeAllAutosaves"
                    text: qsTr("Remove all auto save files")
                    onClicked: {
                        root.app.removeAutosavesOlderThan(new Date(NaN))
                        temporaryFilesWindow.showFiles()
                    }
                }
            }
            RowLayout {
                Label { text: qsTr("Remove files older than") }
                ComboBox { id: olderDay; model: 31; displayText: currentIndex + 1; Accessible.name: qsTr("Day") }
                ComboBox {
                    id: olderMonth
                    model: [qsTr("January"), qsTr("February"), qsTr("March"), qsTr("April"), qsTr("May"), qsTr("June"),
                            qsTr("July"), qsTr("August"), qsTr("September"), qsTr("October"), qsTr("November"), qsTr("December")]
                    Accessible.name: qsTr("Month")
                }
                TextField { id: olderYear; Layout.preferredWidth: 60; validator: IntValidator { bottom: 2012 } Accessible.name: qsTr("Year") }
                Button {
                    objectName: "removeOlderAutosaves"
                    text: qsTr("Remove older auto save files")
                    onClicked: {
                        root.app.removeAutosavesOlderThan(temporaryFilesWindow.cutoff())
                        temporaryFilesWindow.showFiles()
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                onClicked: temporaryFilesWindow.close()
            }
        }
    }

    // O2: the shortcut editor. An installed binding as legacy text ("Ctrl-D"), following installs.
    function boundKeys(symbol, window) {
        return root.hotkeys.keys ? root.hotkeys.accelOf(symbol, window) : ""
    }
    // A mapped button's tooltip (MappedButton::SetToolTip).
    function mappedTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTr("Shortcut can be set using Shift + Click")
    }
    // A bitmap button's tooltip (BitmapButton::SetToolTip): its binding in
    // its window, then the gesture.
    function bitmapTip(text, symbol, window) {
        const key = root.boundKeys(symbol, window)
        return (key.length ? text + " (" + key + ")" : text) + "\n" + qsTr("Shortcut can be set using Shift + Click")
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
        if (!shift && !second)
            return false
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
    // The frame's handlers of the Global window's bindings (HikariSubFrame:
    // OnMenuSelected, OnMenuSelected1, OnChangeLine, OnDelete, OnPageChange,
    // OnPageAdd, OnPageClose, OnAudioSnap, OnUseWindowHotkey), reached from
    // the shell's table and from a Global id bound for a panel. An id below
    // 5000 runs in its own window (OnUseWindowHotkey,
    // HikariSubFrame.cpp:1802-1814). A Global action the rewrite does not
    // have yet still takes its key, as legacy's accelerator does.
    function runGlobalHotkey(symbol) {
        const field = translationText.activeFocus ? translationText : lineText
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
                               "GLOBAL_OPEN_DUMMY_AUDIO", "GLOBAL_OPEN_AUTO_SAVE", "GLOBAL_DELETE_TEMPORARY_FILES",
                               "GLOBAL_SETTINGS", "GLOBAL_QUIT", "GLOBAL_EDITOR", "GLOBAL_ABOUT", "GLOBAL_HELPERS",
                               "GLOBAL_HELP", "GLOBAL_ANSI", "GLOBAL_CHECK_FOR_UPDATES", "GLOBAL_SELECT_FROM_VIDEO",
                               "GLOBAL_PLAY_ACTUAL_LINE", "GLOBAL_STYLE_MANAGER_CLEAN_STYLE"]
        const actions = {
            GLOBAL_SAVE_SUBS: saveAction, GLOBAL_SAVE_ALL_SUBS: saveAllAction, GLOBAL_SAVE_SUBS_AS: saveAsAction,
            GLOBAL_SAVE_TRANSLATION: saveTranslationAction, GLOBAL_REMOVE_SUBS: removeSubsAction,
            GLOBAL_REDO: redoAction, GLOBAL_UNDO: undoAction, GLOBAL_UNDO_TO_LAST_SAVE: undoToLastSaveAction,
            GLOBAL_HISTORY: historyAction, GLOBAL_SEARCH: findAction, GLOBAL_FIND_REPLACE: findReplaceAction,
            GLOBAL_FIND_NEXT: findNextAction, GLOBAL_MISSPELLS_REPLACER: misspellAction,
            GLOBAL_OPEN_SELECT_LINES: selectLinesAction, GLOBAL_OPEN_AUDIO: openAudioAction,
            GLOBAL_AUDIO_FROM_VIDEO: audioFromVideoAction, GLOBAL_CLOSE_AUDIO: closeAudioAction,
            GLOBAL_AUTOMATION_LOAD_SCRIPT: loadScriptAction, GLOBAL_AUTOMATION_RELOAD_AUTOLOAD: reloadAutoloadAction,
            GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT: loadLastScriptAction,
            GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW: automationHotkeysAction, GLOBAL_PLAY_PAUSE: playPauseAction,
            GLOBAL_PREVIOUS_FRAME: previousFrameAction, GLOBAL_NEXT_FRAME: nextFrameAction,
            GLOBAL_SET_VIDEO_AT_START_TIME: goToStartAction, GLOBAL_SET_VIDEO_AT_END_TIME: goToEndAction,
            GLOBAL_GO_TO_NEXT_KEYFRAME: nextKeyframeAction, GLOBAL_GO_TO_PREVIOUS_KEYFRAME: previousKeyframeAction,
            GLOBAL_SET_AUDIO_FROM_VIDEO: setAudioFromVideoAction, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: setAudioMarkFromVideoAction,
            GLOBAL_OPEN_SUBS: openAction, GLOBAL_OPEN_VIDEO: openVideoAction, GLOBAL_OPEN_KEYFRAMES: openKeyframesAction,
            GLOBAL_OPEN_DUMMY_AUDIO: dummyAudioAction, GLOBAL_OPEN_AUTO_SAVE: openAutoSaveAction,
            GLOBAL_DELETE_TEMPORARY_FILES: removeTemporaryAction, GLOBAL_SETTINGS: settingsAction,
            GLOBAL_ABOUT: aboutAction, GLOBAL_HELPERS: creditsAction, GLOBAL_HELP: websiteAction,
            GLOBAL_ANSI: reportIssueAction, GLOBAL_CHECK_FOR_UPDATES: checkForUpdatesAction,
            // P6: OnPageClose (the rewrite's File > Close).
            GLOBAL_CLOSE_PAGE: closeAction
        }
        const items = {
            GLOBAL_OPEN_SPELLCHECKER: checkSpellingItem, GLOBAL_OPEN_ASS_PROPERTIES: assPropertiesItem,
            GLOBAL_OPEN_STYLE_MANAGER: styleManagerItem, GLOBAL_OPEN_SUBS_RESAMPLE: resampleItem,
            GLOBAL_OPEN_FONT_COLLECTOR: fontCollectorItem, // Y8
            GLOBAL_SHOW_SHIFT_TIMES: showShiftTimesItem, GLOBAL_SHIFT_TIMES: runShiftTimesItem,
            GLOBAL_LOAD_EXTERNAL_SESSION: loadSessionFileItem, GLOBAL_SAVE_EXTERNAL_SESSION: saveSessionFileItem,
            GLOBAL_LOAD_LAST_SESSION: loadLastSessionItem
        }
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
                conversionDialog.openFor(conversion[0], conversion[1])
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
                lineText.forceActiveFocus()
                root.video.playActualLine()
            }
            return true
        case "GLOBAL_STYLE_MANAGER_CLEAN_STYLE": // StyleStore::OnCleanStyles
            if (editing) {
                cleanStylesMessage.text = root.styleManager.cleanStyles()
                cleanStylesMessage.open()
            }
            return true
        case "GLOBAL_JOIN_WITH_PREVIOUS": if (editing) root.app.joinLines("previous"); return true
        case "GLOBAL_JOIN_WITH_NEXT": if (editing) root.app.joinLines("next"); return true
        case "GLOBAL_NEXT_TAB": root.app.changeTab(1); return true
        case "GLOBAL_PREVIOUS_TAB": root.app.changeTab(-1); return true
        case "GLOBAL_REMOVE_LINES": if (editing) root.app.deleteLines(); return true
        case "GLOBAL_ADD_PAGE": root.app.addPage(); return true
        }
        // Not in the rewrite yet (docs/qt/coverage.md, O2): the key is taken
        // and nothing runs. GLOBAL_SAVE_WITH_VIDEO_NAME and
        // GLOBAL_VIDEO_INDEXING change nothing in legacy either (OnMenuSelected
        // reads the item's check without switching it), nor do the submenu
        // ids (GLOBAL_SORT_LINES, GLOBAL_SORT_SELECTED_LINES, GLOBAL_RECENT_*).
        return false
    }
    // EditBox::OnAccelerator for an Editor binding in `field` (the focused
    // text field); Video actions bound for the Editor go to the video.
    // False for an action the rewrite does not have yet.
    function runEditorHotkey(action, field) {
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
        if (colours[action] !== undefined) {
            if (colourDialog.openFor(colours[action], field.role, field.selectionStart, field.selectionEnd))
                root.app.colourPickerOpened()
            return true
        }
        if (action.startsWith("EDITBOX_TAG_BUTTON")) {
            const index = Number(action.substring(18)) - 1
            if (index < root.tagButtons.buttons.length)
                root.applyTagButton(index)
            return true
        }
        switch (action) {
        case "EDITBOX_COMMIT_GO_NEXT_LINE": root.editor.commitAndAdvance(); return true
        case "EDITBOX_COMMIT": root.editor.commit(); return true
        case "EDITBOX_SPLIT_LINE": root.editor.splitLine(field.role, field.selectionStart, field.selectionEnd); return true
        case "EDITBOX_SET_DOUBTFUL": root.editor.toggleUnconfirmedAndAdvance(); return true
        case "EDITBOX_START_DIFFERENCE":
        case "EDITBOX_END_DIFFERENCE":
            root.editor.insertTimeDifference(action === "EDITBOX_END_DIFFERENCE", edited.selectionStart, edited.selectionEnd)
            return true
        case "EDITBOX_FIND_NEXT_DOUBTFUL": root.editor.findNextUnconfirmed(); return true
        case "EDITBOX_FIND_NEXT_UNTRANSLATED": root.editor.findNextUntranslated(); return true
        case "EDITBOX_CHANGE_FONT": fontDialog.openFor(field.role, field.selectionStart, field.selectionEnd); return true
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
    function runVideoHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        if (root.hotkeys.repeatedKey(action, 50)) // VideoBox::OnAccelerator (VideoBox.cpp:1137)
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
        }
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, translationText.activeFocus ? translationText : lineText)
        return action.startsWith("GRID_") && root.runGridHotkey(action)
    }
    // SubsGrid::OnAccelerator; Editor and Video actions bound for the Grid
    // go to theirs.
    function runGridHotkey(action) {
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
        }
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, translationText.activeFocus ? translationText : lineText)
        return action.startsWith("VIDEO_") && root.runVideoHotkey(action)
    }
    // AudioBox::OnAccelerator; Video, Editor and Grid actions bound for the
    // Audio window go to theirs (AudioBox::SetAccels).
    function runAudioHotkey(action) {
        if (action.startsWith("GLOBAL_"))
            return root.runGlobalHotkey(action) // the event goes up to the frame
        if (action.startsWith("AUDIO_"))
            return !root.hotkeys.repeatedKey(action) && root.audio.runHotkey(action) // AudioBox.cpp:673
        if (action.startsWith("VIDEO_"))
            return root.runVideoHotkey(action)
        if (action.startsWith("EDITBOX_"))
            return root.runEditorHotkey(action, translationText.activeFocus ? translationText : lineText)
        return action.startsWith("GRID_") && root.runGridHotkey(action)
    }
    HotkeyMapping {
        id: hotkeyMapping
        hotkeys: root.hotkeys
    }
    // GLOBAL_STYLE_MANAGER_CLEAN_STYLE: StyleStore::OnCleanStyles's message
    // (stylestore.cpp:738-775).
    Dialog {
        id: cleanStylesMessage
        objectName: "cleanStylesMessage"
        property alias text: cleanStylesLabel.text
        title: qsTr("Status of deleted styles")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: cleanStylesLabel }
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
                // binding of the same keys wins.
                enabled: !root.menuHasKeys && !root.hotkeys.globalSequences.includes(modelData.keys)
                onActivated: root.automationHotkeys.run(modelData.legacyName)
            }
        }
    }

    // Legacy AutomationHotkeysDialog ("List of automation shortcuts").
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: automationHotkeysWindow
        objectName: "automationHotkeysWindow"
        title: qsTr("List of automation shortcuts")
        width: 800
        height: 360
        flags: Qt.Dialog
        property int selected: -1
        function selectedName() {
            const rows = root.automationHotkeys.rows
            return selected >= 0 && selected < rows.length ? rows[selected].legacyName : ""
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            ListView {
                id: hotkeyList
                objectName: "automationHotkeyList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.automationHotkeys.rows
                header: RowLayout {
                    width: hotkeyList.width
                    Label { text: qsTr("Path and script name"); Layout.preferredWidth: hotkeyList.width * 0.5 }
                    Label { text: qsTr("Macro"); Layout.preferredWidth: hotkeyList.width * 0.35 }
                    Label { text: qsTr("Hotkey"); Layout.fillWidth: true }
                }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: hotkeyList.width
                    highlighted: automationHotkeysWindow.selected === index
                    onClicked: automationHotkeysWindow.selected = index
                    onDoubleClicked: {
                        automationHotkeysWindow.selected = index
                        hotkeyCapture.capture(modelData.legacyName)
                    }
                    Accessible.name: modelData.macro + " " + modelData.keys + " " + modelData.problem
                    contentItem: RowLayout {
                        Label { text: modelData.script.length ? modelData.script : modelData.legacyName; elide: Text.ElideMiddle; Layout.preferredWidth: hotkeyList.width * 0.5 }
                        Label { text: modelData.macro; elide: Text.ElideRight; Layout.preferredWidth: hotkeyList.width * 0.35 }
                        Label {
                            text: modelData.problem.length ? modelData.problem : modelData.keys
                            color: modelData.problem.length ? Theme.warning : palette.windowText
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "automationHotkeysOk"
                    text: qsTr("OK")
                    onClicked: {
                        root.automationHotkeys.commit()
                        automationHotkeysWindow.close()
                    }
                }
                Button {
                    objectName: "mapHotkey"
                    text: qsTr("Map hotkey")
                    enabled: automationHotkeysWindow.selectedName().length > 0
                    onClicked: hotkeyCapture.capture(automationHotkeysWindow.selectedName())
                }
                Button {
                    objectName: "deleteHotkey"
                    text: qsTr("Delete hotkey")
                    enabled: automationHotkeysWindow.selectedName().length > 0
                    onClicked: root.automationHotkeys.clearKeys(automationHotkeysWindow.selectedName())
                }
                Button {
                    objectName: "importLegacyHotkeys"
                    text: qsTr("Import legacy hotkeys…")
                    onClicked: legacyHotkeysDialog.open()
                }
                Button {
                    objectName: "automationHotkeysCancel"
                    text: qsTr("Cancel")
                    onClicked: {
                        root.automationHotkeys.cancel()
                        automationHotkeysWindow.close()
                    }
                }
            }
        }
        FileDialog {
            id: legacyHotkeysDialog
            nameFilters: [qsTr("Hotkeys (Hotkeys.txt)"), qsTr("All files (*)")]
            onAccepted: root.automationHotkeys.importLegacy(root.app.localPath(selectedFile))
        }
    }
    // Legacy HkeysDialog ("Hotkey mapping").
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: hotkeyCapture
        objectName: "hotkeyCapture"
        title: qsTr("Hotkey mapping")
        width: 420
        height: 150
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property string name: ""
        property string keys: ""
        property string conflictWith: ""
        function capture(n) {
            name = n
            keys = ""
            conflictWith = ""
            show()
            captureArea.forceActiveFocus()
        }
        function accept(k) {
            const other = root.automationHotkeys.conflict(name, k)
            if (other.length > 0 && conflictWith !== other) {
                keys = k
                conflictWith = other // asks before replacing, as legacy does
                return
            }
            root.automationHotkeys.setKeys(name, k)
            close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: hotkeyCapture.conflictWith.length
                      ? qsTr("\"%1\" is already assigned to %2. Press it again to replace it.").arg(hotkeyCapture.keys).arg(hotkeyCapture.conflictWith)
                      : qsTr("Please enter a hotkey for \"%1\".").arg(hotkeyCapture.name)
            }
            Item {
                id: captureArea
                objectName: "hotkeyCaptureArea"
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                focus: true
                Keys.onPressed: event => {
                    const k = root.automationHotkeys.keysOf(event.key, event.modifiers)
                    if (k.length > 0) {
                        if (hotkeyCapture.conflictWith.length && k === hotkeyCapture.keys)
                            hotkeyCapture.conflictWith = "" // confirmed: replace
                        hotkeyCapture.accept(k)
                    }
                    event.accepted = true
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Cancel")
                onClicked: hotkeyCapture.close()
            }
        }
    }

    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: reloadPrompt
        objectName: "reloadPrompt"
        title: qsTr("Reloading")
        width: 420
        height: 120
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Subtitles were modified by another program. Reload?")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "reloadYes"
                    text: qsTr("Yes")
                    onClicked: {
                        reloadPrompt.close()
                        root.app.reloadTarget()
                    }
                }
                Button {
                    objectName: "reloadNo"
                    text: qsTr("No")
                    onClicked: reloadPrompt.close()
                }
            }
        }
    }
}
