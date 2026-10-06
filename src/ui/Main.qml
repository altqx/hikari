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
    title: playerVideoName.length > 0 ? qsTr("%1 - HikariSub").arg(playerVideoName)
         : shell.hasEditingTarget ? qsTr("%1 - HikariSub").arg(shell.editingTitle) : "HikariSub"
    color: Theme.background // K2: the application background between panels
    // D2: with the editor off the window is named after the active tab's
    // video (HikariSubFrame::Label(0, true), OnPageChanged).
    readonly property string playerVideoName: {
        if (root.app.editorOn)
            return ""
        const tab = root.app.tabs[root.app.currentTab]
        return tab ? tab.video : ""
    }

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
        if (!result.ok || result.done)
            return // the log window shows the problem; P9: opened in a new tab
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
            // D2: not into the player layout (the tray is part of the Grid).
            if (root.shell.hasReference && !root.workspaceLayout.holding)
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
        // Before the first frame the default arrangement and the saved
        // layout are not in place yet: settle them first.
        root.settleArrangement()
        root.workspaceLayout.resetLayout()
        if (name === "Timing")
            videoDock.close()
        else if (name === "Translation" || name === "Typesetting")
            audioDock.close()
        root.workspaceLayout.preset = name
        root.workspaceLayout.save()
    }

    // D2: legacy View menu (HikariSubFrame.cpp:308-312) as named panel
    // arrangements: label, GLOBAL_VIEW_* id, K1 icon.
    readonly property var viewArrangements: [
        { symbol: "GLOBAL_VIEW_ALL", label: qsTr("All"), objectName: "viewAll", iconRole: "view-all" },
        { symbol: "GLOBAL_VIEW_VIDEO", label: qsTr("Video and subs"), objectName: "viewVideoAndSubs", iconRole: "view-video-subs" },
        { symbol: "GLOBAL_VIEW_AUDIO", label: qsTr("Audio and subs"), objectName: "viewAudioAndSubs", iconRole: "view-audio-subs" },
        { symbol: "GLOBAL_VIEW_ONLY_VIDEO", label: qsTr("Only video"), objectName: "viewOnlyVideo", iconRole: "view-only-video" },
        { symbol: "GLOBAL_VIEW_SUBS", label: qsTr("Only subtitles"), objectName: "viewOnlySubtitles", iconRole: "view-only-subs" }
    ]
    // OnMenuOpened's ViewMenu (HikariSubFrame.cpp:2413-2437): Only subtitles
    // with the editor; All, Video and subs and Only video with a video too,
    // not while the video is full screen on another monitor (V5's
    // IsOnAnotherMonitor); Audio and subs with the audio box (ABox != nullptr).
    function arrangementEnabled(symbol) {
        if (!root.app.editorOn)
            return false
        if (symbol === "GLOBAL_VIEW_AUDIO")
            return root.audio.hasAudio
        if (symbol === "GLOBAL_VIEW_SUBS")
            return true
        return root.video.hasVideo && !(root.videoFullscreen.active && root.videoFullscreen.onAnotherMonitor)
    }
    // HikariSubFrame::OnMenuSelected, GLOBAL_VIEW_ALL..GLOBAL_VIEW_SUBS
    // (HikariSubFrame.cpp:849-892): the arrangement's core panels are shown
    // and the others of Video, Audio, Editor and Grid hidden, each opened
    // panel at its last place in the docked Workspace. The Reference tray is
    // part of the Grid (legacy's comparison was in it); the Timing tool
    // (legacy shiftTimes) is hidden by Only video and comes back with the
    // next arrangement that shows the Grid when it was open. A hidden video
    // pauses. The focus stays where it is when that panel is still shown,
    // otherwise it goes to the Grid, or the first shown panel in F6 order.
    property bool timingHiddenByArrangement: false
    function applyArrangement(symbol) {
        const shown = root.workspaceLayout.arrangementPanels(symbol)
        if (shown.length === 0 || !root.arrangementEnabled(symbol))
            return false
        const focused = root.panels.find(p => p.visible && p.activeFocus) ?? null
        if (!shown.includes("Video") && root.video.playing)
            root.video.pause()
        const core = [videoDock, audioDock, editorDock, gridDock]
        // Panels shown again come back at their places and sizes in the
        // arrangement that last showed all four (legacy laid them out at
        // their own sizes); the tools stay as they are now.
        if (core.every(d => d.isOpen)) {
            root.workspaceLayout.rememberFullArrangement()
        } else if (core.some(d => shown.includes(d.uniqueName) && !d.isOpen)) {
            const tools = root.dockList.filter(d => !core.includes(d))
            const toolsOpen = tools.map(d => d.isOpen)
            if (root.workspaceLayout.restoreFullArrangement()) {
                tools.forEach((d, i) => {
                    if (toolsOpen[i] && !d.isOpen)
                        d.open()
                    else if (!toolsOpen[i] && d.isOpen)
                        d.close()
                })
            }
        }
        for (const dock of core) {
            if (shown.includes(dock.uniqueName))
                dock.open()
            else if (dock.isOpen)
                dock.close()
        }
        const subs = shown.includes("Grid")
        if (subs && root.shell.hasReference)
            referenceDock.open()
        else if (!subs && referenceDock.isOpen)
            referenceDock.close()
        if (!subs && timingDock.isOpen) {
            timingDock.close()
            root.timingHiddenByArrangement = true
        } else if (subs && root.timingHiddenByArrangement) {
            timingDock.open()
            root.timingHiddenByArrangement = false
        }
        root.keepFocusOnAShownPanel(focused)
        root.workspaceLayout.save()
        return true
    }
    // The focus after panels were hidden: `focused` if it is still shown,
    // else the Grid, else the first shown panel in F6 order.
    function keepFocusOnAShownPanel(focused) {
        let next = focused && focused.visible ? focused : null
        if (!next) {
            const shown = root.panels.filter(p => p.visible)
            next = shown.includes(gridPanel) ? gridPanel : shown[0]
            if (next)
                root.focusPanel(next, Qt.OtherFocusReason)
        }
        root.arrangementFocus = next ?? null
        arrangementFocusTimer.restart()
    }
    // The panel an arrangement left the focus on, for a moment (above).
    property var arrangementFocus: null
    Timer {
        id: arrangementFocusTimer
        interval: 1000
        onTriggered: root.arrangementFocus = null
    }

    // D2: GLOBAL_EDITOR (HikariSubFrame::HideEditor, HikariSubFrame.cpp:2009-2091).
    // Off: the editing arrangement is held (nothing saved until the editor
    // comes back) and only the Video panel is shown, docked, without the
    // visual tools' rail and values (HideVideoToolbar, RemoveVisual(false,
    // true)), with the focus; the Search tool, Select lines and the Style
    // manager close (FR, SL, StyleStore hidden). The Video panel cannot be
    // closed meanwhile (legacy's video is the frame's only content, with no
    // close of its own): the Panels menu and the arrangements that could
    // bring a panel back are off. On: the held arrangement comes back as it
    // was, but for the Search tool (FR stays hidden), and the focus goes to
    // the Grid (or the first shown panel). A draft in the Line editor stays
    // as it was either way.
    function applyEditor(on) {
        if (!on) {
            if (root.workspaceLayout.holding)
                return
            if (root.visualTools.activeFamily !== 0)
                root.visualTools.selectFamily(root.visualTools.activeFamily) // back to the crosshair
            if ([videoDock, audioDock, editorDock, gridDock].every(d => d.isOpen))
                root.workspaceLayout.rememberFullArrangement()
            root.workspaceLayout.holdArrangement()
            for (const dock of root.dockList)
                if (dock !== videoDock && dock.isOpen)
                    dock.close()
            if (videoDock.isFloating)
                videoDock.isFloating = false
            videoDock.open()
            videoDock.options = KDDW.KDDockWidgets.DockWidgetOption_NotClosable
            if (selectLinesDialog.visible)
                selectLinesDialog.close()
            if (styleManagerWindow.visible)
                styleManagerWindow.close()
            root.focusPanel(videoPanel, Qt.OtherFocusReason)
        } else {
            videoDock.options = KDDW.KDDockWidgets.DockWidgetOption_None
            if (!root.workspaceLayout.releaseArrangement())
                return
            // Legacy hid FR when the editor went off and nothing shows it
            // again: the Search tool stays closed in the arrangement.
            if (searchDock.isOpen)
                searchDock.close()
            if (root.shell.hasReference && gridDock.isOpen)
                referenceDock.open() // a reference opened meanwhile
            Qt.callLater(() => root.keepFocusOnAShownPanel(null))
        }
    }
    Connections {
        target: root.app
        function onEditorOnChanged() {
            root.applyEditor(root.app.editorOn)
        }
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

    // D3: a panel's "⋯" menu (MuseScore's DockPanelMenuModel,
    // docs/research/musescore-docking.md §1): the panel's own items, then
    // Move panel…, Undock (Dock while it floats) and Close, each disabled
    // where the panel cannot do it (its dock's options: not closable, not
    // dockable). Its header asks for it (Docking.requestMenu: the "⋯"
    // button, a right click on the header, Space or Enter on the button).
    // A panel's own items are settings that have no place in its body or
    // its header's toolbar, never a copy of them: no panel has any yet (the
    // Reference tray's commands are its toolbar's). The Reference tray's
    // Close closes the reference, the tray with it (legacy's close mark,
    // DestroyPreview), so the header holds one close, not two.
    function panelOwnItems(dock) {
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
        readonly property bool closable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotClosable) === 0
        readonly property bool dockable: dock !== null
                                         && (dock.options & KDDW.KDDockWidgets.DockWidgetOption_NotDockable) === 0
        function openFor(target, anchor) {
            dock = target
            // inside a floating panel's frame, not over its drawn shadow
            margins = anchor.Window.window !== root ? Docking.floatingShadow + 1 : 0
            popup(anchor, 0, anchor.height)
        }
        onAboutToShow: Docking.openMenu = dock ? dock.uniqueName : ""
        onAboutToHide: Docking.openMenu = ""
        Instantiator {
            model: panelOptionsMenu.ownItems
            delegate: ShellMenuItem {
                required property var modelData
                objectName: "panelOptions_" + modelData.name
                text: modelData.text
                checkable: modelData.checkable === true
                checked: modelData.checked === true
                enabled: modelData.enabled !== false
                onTriggered: modelData.run()
            }
            onObjectAdded: (index, object) => panelOptionsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => panelOptionsMenu.removeItem(object)
        }
        MenuSeparator {
            objectName: "panelOptionsSeparator"
            visible: panelOptionsMenu.ownItems.length > 0
            height: visible ? implicitHeight : 0
        }
        ShellMenuItem {
            objectName: "panelOptionsMove"
            text: qsTr("Move panel…")
            // D2: not in the player layout, as View > Move panel….
            enabled: panelOptionsMenu.dockable && root.app.editorOn
            onTriggered: placementWindow.openFor(panelOptionsMenu.dock)
        }
        ShellMenuItem {
            objectName: "panelOptionsFloat"
            text: panelOptionsMenu.dock && panelOptionsMenu.dock.isFloating ? qsTr("Dock") : qsTr("Undock")
            enabled: panelOptionsMenu.dock !== null && (!panelOptionsMenu.dock.isFloating || panelOptionsMenu.dockable)
            onTriggered: root.setPanelFloating(panelOptionsMenu.dock, !panelOptionsMenu.dock.isFloating)
        }
        ShellMenuItem {
            objectName: "panelOptionsClose"
            text: panelOptionsMenu.dock === referenceDock ? qsTr("Close reference") : qsTr("Close")
            enabled: panelOptionsMenu.closable
            onTriggered: root.closePanel(panelOptionsMenu.dock)
        }
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
            // D2: a floating panel window that an arrangement brought back
            // takes the activation a moment later; the focus stays where
            // the arrangement put it.
            const kept = root.arrangementFocus
            if (kept && kept.visible && root.workspaceLayout.focusWindow !== null
                    && root.workspaceLayout.focusWindow !== kept.Window.window
                    && root.panels.some(p => p.visible && p.Window.window === root.workspaceLayout.focusWindow)) {
                root.arrangementFocus = null
                root.focusPanel(kept, Qt.OtherFocusReason)
                return
            }
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
                    help: qsTr("Open subtitle file") // HikariSubFrame.cpp:178
                    iconRole: "open-subtitles"
                    action: Action {
                        id: openAction
                        text: qsTr("&Open…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS")) openDialog.open()
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
                    help: qsTr("Remove subtitles from the editor") // HikariSubFrame.cpp:190
                    iconRole: "close-subtitles"
                    objectName: "newMenuItem"
                    // Legacy GLOBAL_REMOVE_SUBS: the tab gets an Untitled default Document.
                    action: Action {
                        id: removeSubsAction
                        text: qsTr("Remove subtitles from the &editor")
                        enabled: root.app.editorOn // D2: OnMenuOpened's FileMenu
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_REMOVE_SUBS")) root.beginClose("new")
                    }
                }
                ShellMenuItem {
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
                    help: qsTr("Opens video file") // HikariSubFrame.cpp:247
                    iconRole: "open-video"
                    objectName: "openVideoMenuItem"
                    action: Action {
                        id: openVideoAction
                        text: qsTr("Open &Video…")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_VIDEO")) videoDialog.show()
                    }
                }
                ShellMenuItem {
                    help: qsTr("Save current file") // HikariSubFrame.cpp:180
                    iconRole: "save"
                    objectName: "saveMenuItem"
                    action: Action {
                        id: saveAction
                        text: qsTr("&Save")
                        enabled: root.app.editorOn && root.editor.editable
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_SUBS")) root.saveSubtitles()
                    }
                }
                ShellMenuItem {
                    help: qsTr("Save all subtitles") // HikariSubFrame.cpp:182
                    iconRole: "save-all"
                    objectName: "saveAllMenuItem"
                    action: Action {
                        id: saveAllAction
                        text: qsTr("Save &all")
                        enabled: root.app.editorOn && root.shell.hasEditingTarget
                        onTriggered: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_ALL_SUBS"))
                                return
                            root.saveAllTabs()
                        }
                    }
                }
                ShellMenuItem {
                    help: qsTr("Save as") // HikariSubFrame.cpp:184
                    iconRole: "save-as"
                    objectName: "saveAsMenuItem"
                    action: Action {
                        id: saveAsAction
                        text: qsTr("Save &as…")
                        enabled: root.app.editorOn && root.shell.hasEditingTarget
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_SAVE_SUBS_AS")) root.openSaveDialog()
                    }
                }
                ShellMenuItem {
                    help: qsTr("Save translation") // HikariSubFrame.cpp:186
                    iconRole: "save-translation"
                    objectName: "saveTranslationMenuItem"
                    action: Action {
                        id: saveTranslationAction
                        text: qsTr("Save &translation")
                        enabled: root.app.editorOn && root.shell.hasEditingTarget && root.editor.translationMode
                        onTriggered: {
                            if (root.hotkeyGesture("GLOBAL_SAVE_TRANSLATION"))
                                return
                            if (root.app.turnOffTranslationMode())
                                root.openSaveDialog()
                        }
                    }
                }
                ShellMenuItem {
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
                    help: qsTr("Opens the selected autosave from the list") // HikariSubFrame.cpp:196
                    objectName: "openAutoSaveMenuItem"
                    action: Action {
                        id: openAutoSaveAction
                        text: qsTr("Open auto save")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_AUTO_SAVE")) recoveryWindow.showBundles()
                    }
                }
                ShellMenuItem {
                    help: qsTr("Opens the temporary file removal window") // HikariSubFrame.cpp:197
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
                    help: qsTr("Last session options") // HikariSubFrame.cpp:200
                    iconRole: "last-session"
                    objectName: "lastSessionMenu"
                    title: qsTr("Last session")
                    ShellMenuItem {
                        help: qsTr("Loads previously loaded files") // HikariSubFrame.cpp:161
                        id: loadLastSessionItem
                        iconRole: "last-session"
                        objectName: "loadLastSessionMenuItem"
                        text: qsTr("Load last session")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_LAST_SESSION")) sessionWindows.load()
                    }
                    ShellMenuItem {
                        help: qsTr("Loads session from saved session file") // HikariSubFrame.cpp:163
                        id: loadSessionFileItem
                        objectName: "loadSessionFileMenuItem"
                        text: qsTr("Load session from file")
                        onTriggered: if (!root.hotkeyGesture("GLOBAL_LOAD_EXTERNAL_SESSION")) sessionWindows.chooseSessionToLoad()
                    }
                    ShellMenuItem {
                        help: qsTr("Saves session to file") // HikariSubFrame.cpp:165
                        id: saveSessionFileItem
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
                        onToggled: root.app.sessionRestore = checked ? 1 : 0
                    }
                    ShellMenuItem {
                        help: qsTr("Loads previously loaded files at program startup") // HikariSubFrame.cpp:172
                        objectName: "loadLastSessionOnStartMenuItem"
                        text: qsTr("Load last session after program start")
                        checkable: true
                        checked: root.app.sessionRestore === 2
                        onToggled: root.app.sessionRestore = checked ? 2 : 0
                    }
                }
                // O1: legacy GLOBAL_SETTINGS, the Options dialog.
                ShellMenuItem {
                    help: qsTr("Program settings") // HikariSubFrame.cpp:202
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
                enabled: root.app.editorOn && root.editor.editable
                id: sortAllMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "sortAll_" + modelData.key
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
                enabled: root.app.editorOn && root.editor.editable
                id: sortSelectedMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "sortSelected_" + modelData.key
                        help: modelData.help
                        text: modelData.label
                        onTriggered: if (!root.hotkeyGesture(modelData.selected)) root.app.sortLines(modelData.key, true)
                    }
                    onObjectAdded: (index, object) => sortSelectedMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortSelectedMenu.removeItem(object)
                }
            }
            ShellMenuItem {
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
                help: qsTr("History") // HikariSubFrame.cpp:216
                iconRole: "history"
                objectName: "historyMenuItem"
                action: Action {
                    id: historyAction
                    text: qsTr("&History")
                    enabled: root.app.editorOn && root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HISTORY")) historyWindow.show()
                }
            }
            ShellMenuItem {
                help: qsTr("Turns on multireplacer") // HikariSubFrame.cpp:241
                iconRole: "multireplace"
                objectName: "misspellMenuItem"
                action: Action {
                    id: misspellAction
                    text: qsTr("Fix minor errors (experimental)")
                    enabled: root.app.editorOn
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_MISSPELLS_REPLACER")) misspellDialog.toggle()
                }
            }
            ShellMenuItem {
                help: qsTr("Selects lines by expressions") // HikariSubFrame.cpp:243
                iconRole: "select-lines"
                objectName: "selectLinesMenuItem"
                action: Action {
                    id: selectLinesAction
                    text: qsTr("Select &lines")
                    enabled: root.app.editorOn && root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SELECT_LINES")) selectLinesDialog.openDialog()
                }
            }
            // F1: legacy GLOBAL_FIND_REPLACE, GLOBAL_SEARCH and GLOBAL_FIND_NEXT.
            ShellMenuItem {
                help: qsTr("Searches for the specified text phrases and replaces them") // HikariSubFrame.cpp:218
                iconRole: "find-replace"
                objectName: "findReplaceMenuItem"
                action: Action {
                    id: findReplaceAction
                    text: qsTr("Find and re&place")
                    enabled: root.app.editorOn && root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_FIND_REPLACE")) root.openSearch(1)
                }
            }
            ShellMenuItem {
                help: qsTr("Searches for the specified text phrase") // HikariSubFrame.cpp:220
                iconRole: "search"
                objectName: "findMenuItem"
                action: Action {
                    id: findAction
                    text: qsTr("&Find")
                    enabled: root.app.editorOn && root.editor.hasLine
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SEARCH")) root.openSearch(0)
                }
            }
            ShellMenuItem {
                help: qsTr("Finds the next occurrence of the phrase in the text") // HikariSubFrame.cpp:222
                iconRole: "search"
                objectName: "findNextMenuItem"
                action: Action {
                    id: findNextAction
                    text: qsTr("Find next")
                    // A question box waits: nothing re-enters the search (legacy's are modal).
                    enabled: root.app.editorOn && root.editor.hasLine && !root.app.findBusy
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
                property string help: qsTr("Open shortcut mapping window") // HikariSubFrame.cpp:355
                text: qsTr("Open shortcut mapping window")
                enabled: root.app.editorOn // D2: OnMenuOpened disables m_AutoMenu's items
                onTriggered: {
                    if (root.hotkeyGesture("GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW"))
                        return
                    root.automationHotkeys.begin()
                    automationHotkeysWindow.show()
                }
            }
            ShellMenuItem {
                help: qsTr("Load script") // HikariSubFrame.cpp:349
                iconRole: "automation"
                objectName: "loadScriptMenuItem"
                action: Action {
                    id: loadScriptAction
                    text: qsTr("&Load script…")
                    enabled: root.app.editorOn
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_SCRIPT")) scriptDialog.open()
                }
            }
            ShellMenuItem {
                help: qsTr("Refresh autoload scripts") // HikariSubFrame.cpp:351
                iconRole: "automation"
                objectName: "reloadAutoloadMenuItem"
                action: Action {
                    id: reloadAutoloadAction
                    text: qsTr("Refresh autoload scripts")
                    enabled: root.app.editorOn
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_RELOAD_AUTOLOAD")) root.automation.reloadAutoload()
                }
            }
            ShellMenuItem {
                iconRole: "run-script"
                objectName: "loadLastScriptMenuItem"
                action: Action {
                    id: loadLastScriptAction
                    text: qsTr("Run the last loaded script")
                    // Legacy's modal progress dialog blocks it while a macro
                    // runs; D2: OnMenuOpened disables m_AutoMenu's items.
                    enabled: root.app.editorOn && !root.automation.running
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT")) root.automation.runLastLoadedScript()
                }
            }
            ShellMenuItem {
                objectName: "rerunMenuItem"
                action: Action {
                    text: qsTr("Rerun last macro")
                    enabled: root.app.editorOn && root.automation.canRerun
                    onTriggered: root.automation.rerunLast()
                }
            }
            ShellMenuItem {
                objectName: "automationManagerMenuItem"
                action: Action {
                    text: qsTr("Automation &manager")
                    enabled: root.app.editorOn
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
                    enabled: root.app.editorOn && !root.automation.running
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
                objectName: "openKeyframesMenuItem"
                help: qsTr("Open keyframes") // HikariSubFrame.cpp:252
                iconRole: "open-keyframes"
                action: Action {
                    id: openKeyframesAction
                    text: qsTr("Open keyframes")
                    enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_KEYFRAMES")) keyframesDialog.show()
                }
            }
            RecentFilesMenu {
                iconRole: "recent-keyframes"
                objectName: "recentKeyframesMenu"
                prefix: "recentKeyframes"
                title: qsTr("Recently opened keyframes")
                enabled: root.app.editorOn && root.video.hasVideo // D2: OnMenuOpened's VidMenu
                load: () => root.app.recentKeyframes()
                onChosen: path => {
                    const problem = root.app.openKeyframesFile(path)
                    if (problem.length > 0)
                        root.log.log(problem)
                }
            }
            ShellMenuItem {
                objectName: "dummyVideoMenuItem"
                action: Action {
                    id: dummyVideoAction
                    text: qsTr("Open dummy video")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_DUMMY_VIDEO")) dummyVideoDialog.show()
                }
            }
            // V6: GLOBAL_SET_START_TIME / GLOBAL_SET_END_TIME (HikariSubFrame.cpp:259-262;
            // OnMenuOpened enables them with a video and the editor).
            ShellMenuItem {
                iconRole: "set-start-time"
                objectName: "setStartTimeMenuItem"
                action: Action {
                    id: setStartTimeAction
                    text: qsTr("Insert start time from video")
                    enabled: root.app.editorOn && root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_START_TIME")) root.app.setTimeFromVideo(false)
                }
            }
            ShellMenuItem {
                iconRole: "set-end-time"
                objectName: "setEndTimeMenuItem"
                action: Action {
                    id: setEndTimeAction
                    text: qsTr("Insert end time from video")
                    enabled: root.app.editorOn && root.video.hasVideo && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_END_TIME")) root.app.setTimeFromVideo(true)
                }
            }
            ShellMenuItem {
                help: qsTr("Go to previous frame") // HikariSubFrame.cpp:263
                iconRole: "frame-previous"
                action: Action {
                    id: previousFrameAction
                    text: qsTr("Previous frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_PREVIOUS_FRAME")) root.video.stepFrames(-1)
                }
            }
            ShellMenuItem {
                help: qsTr("Go to next frame") // HikariSubFrame.cpp:265
                iconRole: "frame-next"
                action: Action {
                    id: nextFrameAction
                    text: qsTr("Next frame"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_NEXT_FRAME")) root.video.stepFrames(1)
                }
            }
            ShellMenuItem {
                help: qsTr("Moves video to start time") // HikariSubFrame.cpp:267
                iconRole: "video-to-start-time"
                action: Action {
                    id: goToStartAction
                    text: qsTr("Go to start time"); enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_START_TIME")) root.video.goToLineStart()
                }
            }
            ShellMenuItem {
                help: qsTr("Moves video to end time") // HikariSubFrame.cpp:270
                iconRole: "video-to-end-time"
                action: Action {
                    id: goToEndAction
                    text: qsTr("Go to end time of line"); enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_VIDEO_AT_END_TIME")) root.video.goToLineEnd()
                }
            }
            ShellMenuItem {
                help: qsTr("Plays / Pauses video") // HikariSubFrame.cpp:273
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
                    text: qsTr("Go to previous keyframe"); enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_PREVIOUS_KEYFRAME")) root.video.previousKeyframe()
                }
            }
            ShellMenuItem {
                iconRole: "keyframe-next"
                action: Action {
                    id: nextKeyframeAction
                    text: qsTr("Go to next keyframe"); enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_GO_TO_NEXT_KEYFRAME")) root.video.nextKeyframe()
                }
            }
            // A3: GLOBAL_SET_AUDIO_FROM_VIDEO, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO
            // (legacy OnMenuOpened: ABox != nullptr && editor; GLOBAL_EDITOR's
            // switch is D2's, the editor itself the editing target's).
            ShellMenuItem {
                iconRole: "audio-to-video-time"
                objectName: "setAudioFromVideoMenuItem"
                action: Action {
                    id: setAudioFromVideoAction
                    text: qsTr("Set audio position to video time")
                    enabled: root.app.editorOn && root.audio.hasAudio && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_AUDIO_FROM_VIDEO")) root.app.setAudioFromVideo(false)
                }
            }
            ShellMenuItem {
                iconRole: "audio-marker-to-video-time"
                objectName: "setAudioMarkFromVideoMenuItem"
                action: Action {
                    id: setAudioMarkFromVideoAction
                    text: qsTr("Set audio marker to video time")
                    enabled: root.app.editorOn && root.audio.hasAudio && root.shell.hasEditingTarget
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_SET_AUDIO_MARK_FROM_VIDEO")) root.app.setAudioFromVideo(true)
                }
            }
            // V3: legacy's video context menu entries (VideoBox.cpp:979-1016)
            // in the Video menu too: Unload video (VIDEO_DELETE_FILE,
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
                iconRole: "zoom"
                objectName: "videoZoomMenuItem"
                action: Action {
                    id: videoZoomAction
                    text: qsTr("Zoom video"); enabled: root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_VIDEO_ZOOM")) root.videoView.toggleZoom()
                }
            }
            ShellMenuItem {
                iconRole: "zoom-reset"
                objectName: "resetVideoZoomMenuItem"
                action: Action {
                    id: resetVideoZoomAction
                    text: qsTr("Turn off video zoom"); enabled: root.videoView.zoomed
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_RESET_VIDEO_ZOOM")) root.videoView.resetZoom()
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
                help: qsTr("Opens audio file") // HikariSubFrame.cpp:293
                iconRole: "open-audio"
                objectName: "openAudioMenuItem"
                action: Action {
                    id: openAudioAction
                    text: qsTr("Open audio")
                    enabled: root.app.editorOn // D2: OnMenuOpened's AudMenu
                    onTriggered: {
                        if (root.hotkeyGesture("GLOBAL_OPEN_AUDIO"))
                            return
                        audioDialog.currentFolder = root.app.audioDialogFolder()
                        audioDialog.open()
                    }
                }
            }
            ShellMenu {
                help: qsTr("Recently opened audio") // HikariSubFrame.cpp:297
                id: recentAudioMenu
                iconRole: "recent-audio"
                objectName: "recentAudioMenu"
                enabled: root.app.editorOn
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
                help: qsTr("Opens audio from video") // HikariSubFrame.cpp:299
                iconRole: "audio-from-video"
                objectName: "audioFromVideoMenuItem"
                action: Action {
                    id: audioFromVideoAction
                    text: qsTr("Open audio from video")
                    enabled: root.app.editorOn && root.video.hasVideo
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_AUDIO_FROM_VIDEO")) root.app.openAudioFromVideo()
                }
            }
            ShellMenuItem {
                help: qsTr("Opens blank audio 2 hour and 30 minutes long") // HikariSubFrame.cpp:301
                objectName: "dummyAudioMenuItem"
                action: Action {
                    id: dummyAudioAction
                    text: qsTr("Open blank 2h30m audio")
                    enabled: root.app.editorOn
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_DUMMY_AUDIO")) root.audio.openDummy()
                }
            }
            ShellMenuItem {
                help: qsTr("Closes audio") // HikariSubFrame.cpp:303
                iconRole: "close-audio"
                objectName: "closeAudioMenuItem"
                action: Action {
                    id: closeAudioAction
                    text: qsTr("Close audio")
                    enabled: root.app.editorOn && root.audio.hasAudio
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
            // D2: legacy's five views as panel arrangements over the docked
            // Workspace (applyArrangement), enabled as OnMenuOpened's
            // ViewMenu (HikariSubFrame.cpp:2413-2437).
            Instantiator {
                model: root.viewArrangements
                delegate: ShellMenuItem {
                    required property var modelData
                    objectName: modelData.objectName
                    iconRole: modelData.iconRole
                    text: modelData.label
                    enabled: root.arrangementEnabled(modelData.symbol)
                    onTriggered: if (!root.hotkeyGesture(modelData.symbol)) root.applyArrangement(modelData.symbol)
                }
                onObjectAdded: (index, object) => viewMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => viewMenu.removeItem(object)
            }
            MenuSeparator {}
            // D1: each panel can be shown (and focused), hidden, floated or
            // docked; Reset layout returns to the Editing arrangement. With
            // the editor off (the player layout) they are disabled, as
            // OnMenuOpened disables the View menu's other items (default:
            // Enable(editor)).
            ShellMenu {
                id: panelsMenu
                objectName: "panelsMenu"
                enabled: root.app.editorOn
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
                enabled: root.app.editorOn
                text: qsTr("&Move panel…")
                onTriggered: placementWindow.openFor(root.focusedDock())
            }
            // Built-in starting arrangements (docs/qt/ux/workspaces.md); the
            // tools they open come with the tool cards.
            ShellMenu {
                id: presetMenu
                objectName: "layoutPresetMenu"
                enabled: root.app.editorOn
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
                enabled: root.app.editorOn
                text: qsTr("&Reset layout")
                onTriggered: root.applyPreset(root.workspaceLayout.preset)
            }
            ShellMenuItem {
                objectName: "restoreLayoutBackup"
                text: qsTr("Restore the previous layout")
                enabled: root.app.editorOn && root.workspaceLayout.hasBackup
                onTriggered: root.workspaceLayout.restoreBackup()
            }
        }
        // Legacy Subtitles menu; its entries join as their cards land.
        ShellMenu {
            objectName: "subtitlesMenu"
            title: qsTr("&Subtitles")
            // D2: GLOBAL_EDITOR, SubsMenu's first item (HikariSubFrame.cpp:316-317),
            // enabled with a DirectShow video or none (OnMenuOpened,
            // HikariSubFrame.cpp:2377-2379): the rewrite's videos are FFMS2's
            // (W1 brings the DirectShow player).
            ShellMenuItem {
                id: editorSwitchItem
                iconRole: "editor"
                objectName: "editorSwitchMenuItem"
                text: qsTr("Enable / Disable editor")
                enabled: !root.video.hasVideo
                onTriggered: if (!root.hotkeyGesture("GLOBAL_EDITOR")) root.app.toggleEditor()
            }
            ShellMenuItem {
                help: qsTr("Shifting subtitle times") // HikariSubFrame.cpp:336
                id: showShiftTimesItem
                iconRole: "shift-times"
                objectName: "showShiftTimes"
                enabled: root.app.editorOn // D2: OnMenuOpened's SubsMenu
                text: qsTr("Shift &times...")
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHOW_SHIFT_TIMES")) root.showPanel(timingDock)
            }
            ShellMenuItem {
                id: runShiftTimesItem
                objectName: "runShiftTimes"
                text: qsTr("Shift times / run time post processor")
                enabled: root.app.editorOn && root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_SHIFT_TIMES")) root.runShiftTimes()
            }
            ShellMenuItem {
                help: qsTr("Is used to manage ASS styles") // HikariSubFrame.cpp:320
                id: styleManagerItem
                iconRole: "styles"
                objectName: "styleManagerMenuItem"
                text: qsTr("Style &manager")
                enabled: root.app.editorOn && root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_STYLE_MANAGER")) styleManagerWindow.showFor(root.app.activeLineStyle())
            }
            ShellMenuItem {
                help: qsTr("ASS subtitle properties") // HikariSubFrame.cpp:318
                id: assPropertiesItem
                iconRole: "script-properties"
                objectName: "assProperties"
                text: qsTr("ASS file properties")
                enabled: root.app.editorOn && root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_ASS_PROPERTIES")) scriptPropertiesDialog.openFor()
            }
            ShellMenu {
                help: qsTr("Converts from one format to another") // HikariSubFrame.cpp:334
                id: conversionMenu
                iconRole: "convert"
                objectName: "conversionMenu"
                enabled: root.app.editorOn
                title: qsTr("Conversion")
                property var targets: []
                onAboutToShow: targets = root.app.conversionTargets()
                Repeater {
                    model: root.conversionItems
                    ShellMenuItem {
                        objectName: "convertTo_" + modelData[0]
                        help: modelData[3]
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
                help: qsTr("Font collector") // HikariSubFrame.cpp:338
                id: fontCollectorItem
                objectName: "fontCollectorMenuItem"
                iconRole: "font-collector"
                text: qsTr("Font collector")
                enabled: root.app.editorOn && root.shell.hasEditingTarget && root.shell.assColumns
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_FONT_COLLECTOR")) fontCollectorDialog.showOnce()
            }
            ShellMenuItem {
                help: qsTr("Resample subtitles") // HikariSubFrame.cpp:340
                id: resampleItem
                iconRole: "resample"
                objectName: "resampleMenuItem"
                text: qsTr("Resample subtitles")
                enabled: root.app.editorOn && root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SUBS_RESAMPLE")) resampleDialog.openDialog()
            }
            // Legacy HikariSubFrame: after Resample subtitles.
            ShellMenuItem {
                help: qsTr("Check spelling") // HikariSubFrame.cpp:342
                id: checkSpellingItem
                iconRole: "spellchecker"
                objectName: "checkSpellingMenuItem"
                text: qsTr("Check spelling")
                enabled: root.app.editorOn && root.shell.hasEditingTarget
                onTriggered: if (!root.hotkeyGesture("GLOBAL_OPEN_SPELLCHECKER")) spellCheckerDialog.openDialog()
            }
            // E6: legacy SubsMenu's last item, "Hides tags in ASS and MDVD"
            // (HikariSubFrame.cpp:344-345): the Grid's switch.
            ShellMenuItem {
                id: hideTagsItem
                iconRole: "hide-tags"
                objectName: "hideTagsMenuItem"
                text: qsTr("Hide tags")
                enabled: root.app.editorOn // D2: OnMenuOpened's SubsMenu
                onTriggered: if (!root.hotkeyGesture("GLOBAL_HIDE_TAGS")) root.app.toggleHideTags()
            }
        }
        ShellMenu {
            title: qsTr("&Help")
            ShellMenuItem {
                help: qsTr("Opens the HikariSub website in the default browser") // HikariSubFrame.cpp:360
                iconRole: "help"
                action: Action {
                    id: websiteAction
                    text: qsTr("HikariSub &website")
                    onTriggered: if (!root.hotkeyGesture("GLOBAL_HELP")) Qt.openUrlExternally("https://altqx.com")
                }
            }
            ShellMenuItem {
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
            ShellMenuItem {
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
        readOnly: !root.editor.editable
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        persistentSelection: true // the Original's selection survives for "Paste the selected"
        Layout.fillWidth: true
        Layout.fillHeight: true
        // E4: the Line's fields take rows below; a short panel keeps a line of text.
        Layout.minimumHeight: topPadding + bottomPadding + cursorRectangle.height

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
            // E5: Moving tags gives the Translated field the focus.
            function onFieldFocusRequested(role) {
                if (role === field.role)
                    field.forceActiveFocus()
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
            ShellMenuItem { iconRole: "edit-copy"; text: qsTr("&Copy"); enabled: field.selectedText.length > 0; onTriggered: field.copy() }
            ShellMenuItem { iconRole: "edit-cut"; text: qsTr("Cu&t"); enabled: field.selectedText.length > 0 && !field.readOnly; onTriggered: field.cut() }
            ShellMenuItem { iconRole: "edit-paste"; text: qsTr("&Paste"); enabled: !field.readOnly; onTriggered: field.paste() }
            MenuSeparator {}
            ShellMenuItem {
                id: spellingOnItem
                iconRole: "spellchecker"
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
                iconRole: "delete"
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
        // TextEditor::OnKeyPress (DialogueTextEditor.cpp:534-545): Tab never
        // goes into the text; it is a navigation event, forward or with
        // Shift backward, to the next control in the tab order (the tag
        // list, which takes its own keys first, closes as the field loses
        // focus). Its window change flag (Ctrl) reached EditBox's
        // HikariContainer::OnNavigation first (HikariPanel.cpp:23), which
        // ignores it, so Ctrl+Tab and Ctrl+Shift+Tab move the same way.
        function tabOut(forward) {
            const next = field.nextItemInFocusChain(forward)
            if (next && next !== field)
                next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        }
        Keys.onShortcutOverride: event => event.accepted = hotkeyAction(event) !== "" || tagListPopup.takesKey(event)
        Keys.onPressed: event => {
            if (tagListPopup.key(event)) {
                event.accepted = true
                return
            }
            const ctrl = event.modifiers & Qt.ControlModifier
            const action = hotkeyAction(event)
            if (action !== "") {
                root.runEditorHotkey(action, field) // an action not here yet still takes the key
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
        // draw over the next panel (the focus ring keeps the margin's room).
        clip: true
        Accessible.role: Accessible.Pane
        Accessible.name: accessibleName
        Accessible.description: accessibleName !== title ? title : ""

        // K2 (visual-language.md, "Keyboard focus"): the panel holding the
        // focus is ringed on its dock header (DockTabBar.qml) in the focus
        // role. D3: no frame of its own; the docking engine's 1-pixel
        // separators are the boundaries between panels (MuseScore's).
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
                dockName: "Video"
                // The controls below the video at their narrowest (the
                // follow choices wrap), above two rail buttons' height.
                minimumBodySize: Qt.size(Math.max(videoTransport.implicitWidth, videoFollow.minimumWidth),
                                         videoControls.implicitHeight + 72)
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
                Keys.onShortcutOverride: event => event.accepted = (event.key === Qt.Key_Escape && root.visualTools.escapable)
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
                // buttons and "Apply to All").
                AssociationOffer {
                    video: root.video
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    z: 1
                }
                // V3: the indexing's progress and Cancel.
                VideoIndexingProgress {
                    video: root.video
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    z: 1
                }
                // T1: the tool rail beside the canvas (layout A).
                // D2: not in the player layout (HideVideoToolbar).
                VisualToolRail {
                    id: visualRail
                    visible: root.app.editorOn
                    tools: root.visualTools
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
                // D2: no visual tool in the player layout (RemoveVisual(false,
                // true): legacy's Visual -1, not even the crosshair); the
                // pointer still reaches the view (zoom, context menu, V5's
                // double-click fullscreen).
                VisualOverlay {
                    id: visualOverlay
                    toolsShown: root.app.editorOn
                    anchors.fill: presenter
                    tools: root.visualTools
                    focusTarget: root.videoFullscreen.active ? videoFullscreenWindow.keyTarget : videoPanel
                    // In fullscreen the pinned panel (m_PanelOnFullscreen) or none.
                    panelHeight: root.videoFullscreen.active ? videoFullscreenWindow.pinnedPanelHeight : videoControls.height
                    view: root.videoView // V4
                    Binding {
                        target: root.videoView; property: "toolsOff"; value: !root.app.editorOn
                    }
                    // V5: without "Show toolbar" the pointer shows (and after
                    // a second hides) over the fullscreen picture.
                    cursorOverride: !root.videoFullscreen.active || root.videoFullscreen.showToolbar ? -1
                                    : videoFullscreenWindow.cursorHidden ? Qt.BlankCursor : Qt.ArrowCursor
                    onContextMenuRequested: (x, y) => {
                        if (root.videoFullscreen.active)
                            videoFullscreenWindow.openMenuAt(Qt.point(x, y))
                        else
                            videoContextMenu.openAt(Qt.point(x, y))
                    }
                    onFullScreenRequested: root.videoDoubleClick()
                    onPointerMoved: (x, y) => {
                        if (root.videoFullscreen.active)
                            videoFullscreenWindow.pointerMoved(visualOverlay.mapToItem(null, x, y).y)
                    }
                }
                // V4: the zoom mode's frame, the context menu and the aspect ratio.
                VideoZoomFrame {
                    anchors.fill: presenter
                    view: root.videoView
                }
                }
                VideoContextMenu {
                    id: videoContextMenu
                    shell: root
                    function openAt(point) {
                        at = point
                        popup(visualOverlay, point)
                    }
                    onOpenVideoRequested: videoDialog.open()
                    onOpenSubtitlesRequested: openDialog.open()
                    onAspectRatioRequested: aspectRatioDialog.openAtCursor()
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
                VisualToolValues { // T1-T4: the family's options, values and batch picker
                    shownInLayout: root.app.editorOn // D2: part of legacy's video toolbar
                    Layout.fillWidth: true
                    // It wraps rather than widen the column: wider text (a
                    // translation, a larger font) or a narrow panel must not
                    // push Next frame out of the panel.
                    Layout.minimumWidth: 0
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
                        Accessible.name: qsTr("Video times")
                        // Legacy's times field: its parts named in the tooltip.
                        Accessible.description: qsTr("Frame time; frame number; frames from the line's start frame; "
                                                     + "milliseconds from the line's start and end")
                        HoverHandler { id: timesHover }
                        ToolTip.visible: timesHover.hovered && text.length > 0
                        ToolTip.text: Accessible.description
                    }
                    VisualToolReadout { tools: root.visualTools } // T1: the tool's read-only values
                }
                VideoFollowChoices { id: videoFollow; Layout.fillWidth: true; settings: root.app.settings } // V6
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
                        text: qsTr("Previous file")
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Previous file"), "VIDEO_PREVIOUS_FILE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_PREVIOUS_FILE", 3, "bitmap")) videoFileQuestion.ask(false)
                    }
                    IconToolButton {
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
                    IconToolButton {
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
                    IconToolButton {
                        objectName: "stopVideo"
                        iconRole: "media-stop"
                        text: qsTr("Stop")
                        enabled: root.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Stop"), "VIDEO_STOP", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_STOP", 3, "bitmap")) root.video.stop()
                    }
                    IconToolButton {
                        objectName: "nextFile"
                        iconRole: "media-next-file"
                        text: qsTr("Next file")
                        focusPolicy: Qt.NoFocus
                        tip: root.bitmapTip(qsTr("Next file"), "VIDEO_NEXT_FILE", 3)
                        onClicked: if (!root.hotkeyGesture("VIDEO_NEXT_FILE", 3, "bitmap")) videoFileQuestion.ask(true)
                    }
                    IconToolButton {
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
                    IconToolButton {
                        objectName: "nextFrame"
                        iconRole: "frame-next"
                        text: qsTr("Next frame")
                        enabled: root.video.hasVideo && root.video.frame + 1 < root.video.frameCount
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
            title: qsTr("Audio")
            Panel {
                id: audioPanel
                anchors.fill: parent
                objectName: "audioPanel"
                dockName: "Audio"
                // With audio open: the button row under a short display.
                minimumBodySize: audioButtons.visible
                                 ? Qt.size(audioButtons.implicitWidth, 64 + audioScroll.implicitHeight + 4 + audioButtons.implicitHeight)
                                 : Qt.size(audioStatus.implicitWidth, audioStatus.implicitHeight)
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
                dockName: "Editor"
                // The rows that do not wrap; the Line inspector's fields
                // wrap onto two rows below 850 (and its narrow layout fits
                // these), so its width-dependent implicit width is not used,
                // nor the translation buttons' (they wrap too). In height,
                // the tag buttons and a line of each text field shown: the
                // rest scrolls.
                minimumBodySize: Qt.size(Math.max(tagRow.implicitWidth, editorCounters.implicitWidth),
                                         tagRow.implicitHeight + editorColumn.spacing + lineText.Layout.minimumHeight
                                         + (translationText.visible ? editorColumn.spacing
                                            + translationText.Layout.minimumHeight : 0))
                title: shell.hasEditingTarget ? qsTr("Line editor: %1").arg(shell.editingTitle)
                                              : qsTr("Line editor")
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
                    root.runEditorHotkey(action, translationText.activeFocus ? translationText : lineText)
                    event.accepted = true
                }
                // A dock shorter than the Line editor's rows (translation mode
                // in the default 1280 x 800 layout) scrolls them, with a thin
                // bar beside them; nothing is cut off below the dock's edge.
                // The wheel and the bar scroll; a mouse drag stays the text
                // fields' selection, and touch flicks. The scrolled area
                // takes the whole panel with the body's margin inside it, so
                // focus rings keep the room they had at the panel's edges.
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
                        const room = 5 // the focus ring's (FocusRing: 3 out, 2 wide)
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
                        // the body's margin, in the scrolled area
                        x: 4
                        y: 4
                        width: editorScroll.contentWidth - 8
                        // The rows' own heights, or the dock's when it is taller
                        // (the text fields take the rest).
                        height: Math.max(editorScroll.height - 8, implicitHeight)

                        // The tag and colour buttons (legacy BoxSizer4).
                        RowLayout {
                            id: tagRow
                            Layout.fillWidth: true
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
                                        root.colourClick(modelData.number, true, translationText.activeFocus ? translationText : lineText)
                                    }
                                    // Y7: the right click (wxEVT_RIGHT_UP, EditBox.cpp:184-196).
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton
                                        onTapped: if (parent.enabled) root.colourClick(parent.modelData.number, false,
                                                                                    translationText.activeFocus ? translationText : lineText)
                                    }
                                }
                            }
                            // E4: Text position (legacy Ban, after the colours).
                            AlignmentChoice {
                                editor: root.editor
                                onChosen: (root.editor.translationMode && root.editor.translationText.length ? translationText : lineText).forceActiveFocus()
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
                            IconToolButton { // legacy's square MenuButton with ARROW_LIST_DOUBLE
                                objectName: "manageTagButtons"
                                iconRole: "menu-more"
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
                            // The hidden-tag view's switch (E6: the hide-tags icon,
                            // checked while tags are hidden).
                            IconToolButton {
                                objectName: "showTags"
                                iconRole: "hide-tags"
                                text: qsTr("Hide tags")
                                checkable: true
                                checked: !root.editor.showTags
                                onToggled: root.editor.showTags = !checked
                            }
                        }
                        // E5: legacy BoxSizer5, the row under the tag buttons
                        // that holds "Translator mode" (EditBox.cpp:233-239, 308).
                        RowLayout {
                            TranslatorModeCheck { app: root.app; editor: root.editor }
                        }

                        // E4: Wraps, characters per second and Time/Frames (legacy BoxSizer5).
                        LineCounters {
                            id: editorCounters
                            editor: root.editor
                            Layout.fillWidth: true
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
                        // They wrap: a narrow editor, a larger font or longer
                        // translated labels must not push them past the panel's
                        // edge, out of reach.
                        Flow {
                            objectName: "translationButtons"
                            visible: root.editor.translationMode
                            Layout.fillWidth: true
                            spacing: 5
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
                            TranslationToggles { app: root.app; editor: root.editor } // E5
                        }

                        // E4: the Line's fields (legacy BoxSizer2, below the text).
                        LineInspector {
                            editor: root.editor
                            hotkeys: root.hotkeys
                            Layout.fillWidth: true
                            onStyleEditRequested: style => styleManagerWindow.showFor(style)
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
                dockName: "Grid"
                minimumBodySize: Qt.size(240, grid.rowHeight * 3) // the header and two Lines
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
                    onLineClicked: (id, modifiers, endColumn, doubleClick) => root.app.clickLine(id, modifiers, endColumn, doubleClick)
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
                            iconRole: "duplicate"
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
                        // V6: after "Split lines", as legacy's menu (SubsGrid.cpp:264-265).
                        ShellMenuItem {
                            objectName: "selectVisibleLines"; text: qsTr("Select all lines visible on video")
                            onTriggered: if (!root.gridGesture(this, "GRID_SELECT_VISIBLE_LINES")) root.app.selectLinesVisibleOnVideo()
                        }
                        ShellMenuItem { objectName: "makeTree"; text: qsTr("Make tree"); onTriggered: if (!root.gridGesture(this, "GRID_TREE_MAKE")) root.app.makeGroups() }
                        // R2: legacy's in-Grid preview, here another tab in the reference tray (SubsGrid.cpp:277).
                        ShellMenuItem {
                            iconRole: "reference"
                            objectName: "showPreview"
                            readonly property string keys: root.boundKeys("GRID_SHOW_PREVIEW", 1)
                            text: qsTr("Show subtitles preview") + (keys.length ? "\t" + keys : "")
                            enabled: gridMenu.visible && root.app.canShowPreview()
                            onTriggered: if (!root.gridGesture(this, "GRID_SHOW_PREVIEW")) root.app.showPreview()
                        }
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
                            iconRole: "filter"
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
                        ShellMenuItem { iconRole: "edit-copy"; objectName: "copyLines"; text: qsTr("Copy\tCtrl+C"); onTriggered: if (!root.gridGesture(this, "GRID_COPY")) root.app.copyLines() }
                        ShellMenuItem { iconRole: "edit-cut"; objectName: "cutLines"; text: qsTr("Cut\tCtrl+X"); onTriggered: if (!root.gridGesture(this, "GRID_CUT")) root.app.cutLines() }
                        ShellMenuItem { iconRole: "edit-paste"; objectName: "pasteLines"; text: qsTr("Paste\tCtrl+V"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE")) root.app.pasteLines() }
                        ShellMenuItem { objectName: "copyColumns"; text: qsTr("Copy columns"); onTriggered: if (!root.gridGesture(this, "GRID_COPY_COLUMNS")) columnsWindow.choose(false) }
                        ShellMenuItem { objectName: "pasteColumns"; text: qsTr("Paste columns"); onTriggered: if (!root.gridGesture(this, "GRID_PASTE_COLUMNS")) columnsWindow.choose(true) }
                        // E6: SubsGrid's menu, "Delete text" before "Delete" (SubsGrid.cpp:285-286).
                        ShellMenuItem { objectName: "deleteText"; text: qsTr("Delete text"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_TEXT")) root.app.deleteText() }
                        ShellMenuItem { iconRole: "delete"; objectName: "deleteLines"; text: qsTr("Delete lines\tShift+Del"); onTriggered: if (!root.gridGesture(this, "GLOBAL_REMOVE_LINES")) root.app.deleteLines() }
                        // Y8: SubsGrid's menu (SubsGrid.cpp:286-288).
                        MenuSeparator {}
                        ShellMenuItem {
                            iconRole: "font-collector"
                            objectName: "gridFontCollector"; text: qsTr("Font collector"); enabled: root.shell.assColumns
                            onTriggered: if (!root.gridGesture(this, "GLOBAL_OPEN_FONT_COLLECTOR")) fontCollectorDialog.showOnce()
                        }
                        // Y9: SubsGrid.cpp:289, enabled for a ".mkv" or ".ogm" video.
                        ShellMenuItem {
                            iconRole: "extract-subtitles"
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
                dockName: "Reference"
                minimumBodySize: Qt.size(240, referenceTray.grid.rowHeight * 3)
                visible: shell.hasReference
                title: qsTr("Reference (protected, read-only): %1").arg(shell.referenceTitle)
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
            title: qsTr("Shift times")
            Panel {
                id: timingPanel
                objectName: "timingPanel"
                dockName: "Timing"
                minimumBodySize: Qt.size(shiftForm.implicitWidth + 20, 120) // the form scrolls
                anchors.fill: parent
                title: qsTr("Shift times")
                ScrollView {
                    id: shiftScroll
                    anchors.fill: parent
                    clip: true
                    // The scroll bar shows while the form is taller than the
                    // panel (the post processor and profiles lay below it unseen).
                    ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
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
                                IconToolButton { objectName: "shiftProfileSave"; iconRole: "add"; text: qsTr("Adding and editing profiles"); onClicked: root.shiftTimes.saveProfile(shiftProfile.editText) }
                                IconToolButton { objectName: "shiftProfileRemove"; iconRole: "remove"; text: qsTr("Removing profiles"); onClicked: root.shiftTimes.removeProfile(shiftProfile.editText) }
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
                dockName: "Search"
                minimumBodySize: searchTool.minimumSize
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
            // D3: the horizontal panels keep a one-tab header even alone;
            // the Reference tray's toolbar sits in its header.
            Docking.setPanelHeader("Grid", true, null)
            Docking.setPanelHeader("Reference", true, referenceTray.toolbar)
            Docking.setPanelHeader("Search", true, null)
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
        root.workspaceLayout.restoreSaved()
        // D2: HikariSubFrame's constructor, `if (!EDITOR_ON) HideEditor(false)`.
        if (!root.app.editorOn)
            root.applyEditor(false)
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
        onTriggered: {
            // D2: the arrangement View > All returns to, while all four show.
            if ([videoDock, audioDock, editorDock, gridDock].every(d => d.isOpen))
                root.workspaceLayout.rememberFullArrangement()
            root.workspaceLayout.save()
        }
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
        // P9: the tab menu's Save, Save all and Close all tabs.
        onSaveRequested: id => root.saveSubtitles(id)
        onSaveAllRequested: root.saveAllTabs()
        onCloseAllRequested: tabCommands.confirmCloseAll()
      }
      // P10: legacy's status bar. Its first field is help and progress text
      // (Automation's set_status_text, menu help, "Autosave"), never the
      // editing target, which the Document tab and the window title name.
      StatusBar {
        shell: root.shell
        fieldsController: root.statusBar
        selectionText: root.shell.selectionStatus
        // The last save's message only: the tab's modified mark says
        // "Modified" (said once, visual-language.md).
        saveText: root.editor.saveStatus
        Layout.fillWidth: true
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
    // P9: for any tab's Document `id` (none: the editing target), and Save
    // all's queue of Documents that need the dialog, one after another.
    function saveSubtitles(id) {
        const document = id ?? 0
        const route = root.app.saveRouteFor(document)
        if (route === "dialog")
            openSaveDialog(document)
        else if (route === "readonly") {
            readOnlyWarning.document = document
            readOnlyWarning.open()
        } else if (document === 0)
            root.editor.save()
        else
            root.app.saveDocument(document)
    }
    property var saveQueue: []
    function saveAllTabs() {
        saveQueue = root.app.saveAll()
        saveNext()
    }
    function saveNext() {
        if (saveQueue.length === 0)
            return
        const next = saveQueue[0]
        saveQueue = saveQueue.slice(1)
        if (next.route === "readonly") {
            readOnlyWarning.document = next.id
            readOnlyWarning.open()
        } else {
            openSaveDialog(next.id)
        }
    }
    function openSaveDialog(id) {
        const v = root.app.saveDialogValuesFor(id ?? 0)
        saveAsDialog.document = id ?? 0
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
        property var document: 0 // P9: the Document saved (0: the editing target)
        onAccepted: {
            if (root.app.saveChosenFor(document, selectedFile) === "readonly") {
                readOnlyWarning.document = document
                readOnlyWarning.open()
            } else {
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
        property var document: 0
        onClosed: root.openSaveDialog(document)
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
            loadAction: loadScriptAction
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
        objectName: "videoOpenDialog"
        nameFilters: [qsTr("Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"), qsTr("All files (*)")]
        // P9; V5: from the fullscreen window without the subtitles question.
        onAccepted: root.videoFullscreen.active ? root.openVideoUnasked(root.app.localPath(selectedFile))
                                                : tabCommands.openVideoFile(root.app.localPath(selectedFile))
        // V3: the subtitles' folder, else the latest recent video's
        function show() {
            currentFolder = root.app.videoDialogFolder()
            open()
        }
    }
    // V3: GLOBAL_OPEN_DUMMY_VIDEO and the folder walk's question.
    DummyVideoDialog {
        id: dummyVideoDialog
        app: root.app
        anchors.centerIn: parent
        onRefused: message => root.log.log(message)
    }
    VideoFileQuestion {
        id: videoFileQuestion
        app: root.app
        anchors.centerIn: parent
    }

    // V4: VIDEO_ASPECT_RATIO.
    AspectRatioDialog {
        id: aspectRatioDialog
        view: root.videoView
    }

    // V5: the fullscreen video window (legacy Fullscreen). What it asks
    // shows in it, as legacy parented its dialogs to m_FullScreenWindow
    // (VideoBox.cpp:895, 907, 1087, 1103).
    VideoFullscreen {
        id: videoFullscreenWindow
        shell: root
        controller: root.videoFullscreen
        onOpenVideoRequested: videoDialog.show()
        onOpenSubtitlesRequested: openDialog.open()
        onAspectRatioRequested: aspectRatioDialog.openAtCursor()
        onFileQuestionRequested: next => videoFileQuestion.ask(next)
        Component.onCompleted: {
            root.videoFullscreen.setWindows(videoFullscreenWindow, root)
            root.videoFullscreen.setFocusFallback(videoPanel)
        }
    }
    Binding {
        when: root.videoFullscreen.active
        target: aspectRatioDialog; property: "parent"; value: videoFullscreenWindow.contentItem
    }
    Binding {
        when: root.videoFullscreen.active
        target: videoFileQuestion; property: "parent"; value: videoFullscreenWindow.contentItem
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
            // V3: the recent keyframes take the file too (SetRecent(3))
            const problem = root.app.openKeyframesFile(selectedFile.toString())
            if (problem.length > 0)
                root.log.log(problem)
        }
        // V3: the video's folder, else the latest recent keyframes'
        function show() {
            currentFolder = root.app.keyframesDialogFolder()
            open()
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
            // Legacy's description, behind an info button rather than
            // printed under the buttons for good.
            IconToolButton {
                objectName: "translationShiftDescription"
                Layout.alignment: Qt.AlignRight
                iconRole: "about"
                text: qsTr("Description")
                tip: qsTr("Description:\nOriginal - subtitle text with correct timing, used to compare pasted dialogue lines; it is deleted later.\nTranslation - text pasted into subtitles with correct timing.")
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
        header: IconDialogHeader { objectName: "aboutDialogTitle"; iconRole: "about"; text: aboutDialog.title }
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
                  // W2: legacy's two lines (HikariSubFrame.cpp:1123-1124), with their licences.
                  (root.app.includesCsri ? "CSRI - Copyright © David Lamparter (BSD licence).\n" : "") +
                  (root.app.includesVsfilter ? "Vsfilter - Copyright © Gabest (GNU GPL 2 or later).\n" : "") +
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
        header: IconDialogHeader { objectName: "creditsDialogTitle"; iconRole: "credits"; text: creditsDialog.title }
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
                    Label { text: "×" }
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
        title: qsTr("Resample subtitles") // the menu command's name (legacy: "Change resolution")
        header: IconDialogHeader { objectName: "resampleDialogTitle"; iconRole: "resample"; text: resampleDialog.title }
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
                    Label { text: "×" }
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
                    Label { text: "×" }
                    SpinBox { id: targetHeight; objectName: "resampleHeight"; from: 100; to: 10000; editable: true }
                    Button {
                        text: qsTr("From video")
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
                    text: qsTr("OK")
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
    SimpleColourPicker {
        id: simpleColourPicker
        editor: root.editor
        picker: root.colourPicker
    }
    // Y7: EditBox::AllColorClick (EditBox.cpp:862-919): a left click and the
    // hotkey open "Choose color", a right click the simple "Color picker";
    // COLORPICKER_SWITCH_CLICKS swaps them.
    function colourClick(number, leftClick, field) {
        if (root.colourPicker.switchClicks)
            leftClick = !leftClick
        if (!leftClick)
            simpleColourPicker.openFor(number, field.role, field.selectionStart, field.selectionEnd)
        else if (colourDialog.openFor(number, field.role, field.selectionStart, field.selectionEnd))
            root.app.colourPickerOpened()
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
        root.applyMenuShortcuts()
        if (root.app.recoveryBundles().length > 0)
            recoveryWindow.showBundles()
    }
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: recoveryWindow
        objectName: "recoveryWindow"
        title: qsTr("Open auto save")
        width: 560
        height: 560
        flags: Qt.Dialog
        property var bundles: []
        function showBundles() {
            bundles = root.app.recoveryBundles()
            legacyAutosaves.reload() // P9
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
            // P9: the legacy Subs/ autosaves, read only.
            LegacyAutosaveList {
                id: legacyAutosaves
                app: root.app
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpened: recoveryWindow.close()
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
        // As wide as its rows need (the last row's button was cut at 520).
        width: Math.max(520, temporaryContent.implicitWidth + 16)
        minimumWidth: temporaryContent.implicitWidth + 16
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
            id: temporaryContent
            anchors.fill: parent
            anchors.margins: 8
            Label { text: qsTr("Auto save") }
            Label {
                visible: temporaryFilesWindow.bundles.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.muted
                text: qsTr("No auto save files")
            }
            ListView {
                visible: temporaryFilesWindow.bundles.length > 0
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
        GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW: automationHotkeysAction, GLOBAL_PLAY_PAUSE: playPauseAction,
        GLOBAL_PREVIOUS_FRAME: previousFrameAction, GLOBAL_NEXT_FRAME: nextFrameAction,
        GLOBAL_SET_VIDEO_AT_START_TIME: goToStartAction, GLOBAL_SET_VIDEO_AT_END_TIME: goToEndAction,
        GLOBAL_SET_START_TIME: setStartTimeAction, GLOBAL_SET_END_TIME: setEndTimeAction, // V6
        GLOBAL_GO_TO_NEXT_KEYFRAME: nextKeyframeAction, GLOBAL_GO_TO_PREVIOUS_KEYFRAME: previousKeyframeAction,
        GLOBAL_SET_AUDIO_FROM_VIDEO: setAudioFromVideoAction, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: setAudioMarkFromVideoAction,
        GLOBAL_VIDEO_ZOOM: videoZoomAction, GLOBAL_RESET_VIDEO_ZOOM: resetVideoZoomAction, // V4
        GLOBAL_OPEN_SUBS: openAction, GLOBAL_OPEN_VIDEO: openVideoAction, GLOBAL_OPEN_KEYFRAMES: openKeyframesAction,
        GLOBAL_OPEN_DUMMY_AUDIO: dummyAudioAction, GLOBAL_OPEN_AUTO_SAVE: openAutoSaveAction,
        GLOBAL_OPEN_DUMMY_VIDEO: dummyVideoAction, // V3
        GLOBAL_DELETE_TEMPORARY_FILES: removeTemporaryAction, GLOBAL_SETTINGS: settingsAction,
        GLOBAL_ABOUT: aboutAction, GLOBAL_HELPERS: creditsAction, GLOBAL_HELP: websiteAction,
        GLOBAL_ANSI: reportIssueAction, GLOBAL_CHECK_FOR_UPDATES: checkForUpdatesAction,
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
        GLOBAL_EDITOR: editorSwitchItem // D2: HideEditor
    }
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
        for (let i = 0; i < root.menuBar.count; ++i)
            walk(root.menuBar.menuAt(i))
    }
    Connections {
        target: root.hotkeys
        function onInstalledChanged() { root.applyMenuShortcuts() }
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
        const actions = root.globalActions()
        const items = root.globalItems()
        if (menuSelected1.includes(symbol) && root.hotkeys.repeatedKey(symbol))
            return true
        // V5: in fullscreen the frame's accelerators reach VideoBox::OnAccelerator,
        // whose GLOBAL_EDITOR is OpenEditor (VideoBox.cpp:1162), not the main
        // window's editor switch (D2).
        if (symbol === "GLOBAL_EDITOR" && root.videoFullscreen.active) {
            root.app.openEditorFromFullScreen()
            return true
        }
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
            // OnMenuOpened: the item is enabled for the formats it converts to
            // (and with the editor, D2).
            if (root.app.editorOn && root.app.conversionTargets().indexOf(conversion[0]) >= 0)
                conversionDialog.openFor(conversion[0], conversion[1])
            return true
        }
        const sort = root.sortKeys.find(k => k.all === symbol || k.selected === symbol)
        if (sort) {
            if (root.app.editorOn && root.editor.editable)
                root.app.sortLines(sort.key, sort.selected === symbol)
            return true
        }
        // D2: OnMenuSelected's GLOBAL_VIEW_* (the item's state is checked first).
        if (root.workspaceLayout.arrangementPanels(symbol).length > 0) {
            root.applyArrangement(symbol)
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
        // V5 (VideoBox.cpp:1161-1162): SetFullscreen(), and GLOBAL_EDITOR's
        // OpenEditor when it reaches the video (in fullscreen).
        case "VIDEO_FULL_SCREEN": root.videoFullscreen.toggle(0); return true
        case "VIDEO_ASPECT_RATIO": aspectRatioDialog.openAtCursor(); return true
        case "VIDEO_SAVE_FRAME_TO_PNG":
        case "VIDEO_COPY_FRAME_TO_CLIPBOARD":
        case "VIDEO_SAVE_SUBBED_FRAME_TO_PNG":
        case "VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD":
            root.videoView.snapshot(action)
            return true
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
