// O1: legacy OptionsDialog "Options" (GLOBAL_SETTINGS, File > Settings) over
// the settings registry. `values` is what each control holds
// (application::OptionsState); OK/Apply write the bound controls whose value
// differs (legacy SetOptions). Theme files are excluded by the accepted
// settings decision; the Appearance page (K2, AppearancePage.qml) takes the
// Themes page's place and previews its choices live until OK, Apply or
// Cancel; the Hotkeys page is the shortcut editor's (O2, `hotkeys`);
// Associations are Windows only and wait for their platform card.
//
// PF1 (#225), after MuseScore 4's Preferences (docs/research/musescore-shell.md
// §PF1): the window "Settings" on DG1's frame (resizable, its place and size
// kept, modal to the main window), a flat page list (legacy's sub-pages are
// sections now: PF1-pages), each page one column of titled sections
// (SettingsSection, SettingsRow), the page shown last opened again, also
// after a restart. "Reset page" and "Reset all settings…" are staged like
// any change (PF1-staged-reset): OK or Apply keeps them, Cancel, Esc and the
// window's Close drop them.
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "settingsDialog"
    required property var app
    // SY2: the Appearance page's colour popover.
    required property ColourPickerController colourPicker
    property var hotkeys: null // O2: the Hotkeys page's HotkeysController
    property var updates: null // PU7: the Updates page's UpdateChecker (what this install offers)
    property TagButtonsController tagButtons: null // LE5: the Line editor page's tag button list
    title: qsTr("Settings")
    dialogId: "settings"
    settings: app ? app.settings : null
    modal: true
    resizable: true
    flush: true
    defaultButton: okButton
    property var values: ({})
    property var languages: []
    property var dictionaries: []
    property var catalogs: []
    property var styles: []
    property var renderers: [] // W2: the CSRI renderers' names and "libass", VSFilterMod last
    property var rendererLabels: [] // W2: as shown, VSFilterMod's with its note
    property var outputDevices: [] // H2: System default, the connected devices, a missing choice
    property ToastStack notices: null
    // LE5: the tag buttons as the Line editor page holds them, {name, tag,
    // type, icon} each, written on OK or Apply where they differ.
    property var tagButtonList: []
    // "Reset all settings…" since the last OK or Apply: OK or Apply resets
    // the store first (PF1-staged-reset).
    property bool resetAllStaged: false
    signal reloaded()
    // VF1: the follow lists' texts, shared with the Video panel's menu.
    readonly property VideoFollowChoices follow: VideoFollowChoices { settings: dialog.app.settings }

    // The pages in the list's order, by id (the C++ OptionsPage ids, and
    // "hotkeys"). PU7 (#309): Updates after Hotkeys (MuseScore 4's Update
    // after Shortcuts).
    readonly property var pages: [
        {id: "general", name: qsTr("General"), iconRole: "settings"},
        {id: "appearance", name: qsTr("Appearance"), iconRole: "appearance"},
        {id: "lineEditor", name: qsTr("Line editor"), iconRole: "editor"},
        {id: "grid", name: qsTr("Subtitle grid"), iconRole: "settings-grid"},
        {id: "video", name: qsTr("Video"), iconRole: "settings-video"},
        {id: "audio", name: qsTr("Audio"), iconRole: "settings-audio"},
        {id: "conversion", name: qsTr("Conversion"), iconRole: "convert"},
        {id: "subtitleProperties", name: qsTr("Subtitle properties"), iconRole: "script-properties"},
        {id: "automation", name: qsTr("Automation"), iconRole: "automation"},
        {id: "hotkeys", name: qsTr("Hotkeys"), iconRole: "hotkeys"},
        {id: "updates", name: qsTr("Updates"), iconRole: "check-updates"}
    ]
    readonly property string currentPage: pages[Math.max(0, pageList.currentIndex)].id
    function pageIndex(id) { return pages.findIndex(p => p.id === id) }

    // `page`: the page to show by id (AV1's "Audio settings…" asks for
    // "audio"); without one, the page shown last. AM1: `filter` is the
    // Hotkeys page's filter to choose (the Automation panel's "Shortcuts in
    // Settings…", MuseScore's deep link from a plugin to its shortcut).
    function openDialog(page, filter) {
        const r = app.openSettingsDialog()
        languages = r.languages
        dictionaries = r.dictionaries
        catalogs = r.catalogs
        styles = r.styles
        renderers = r.renderers
        rendererLabels = r.rendererLabels
        outputDevices = r.outputDevices
        values = r.values
        resetAllStaged = false
        copyTagButtons()
        reloaded()
        hotkeyFilter.currentIndex = 0 // a new dialog's filter choice
        hotkeySearch.text = ""
        const shown = pageIndex(page ?? (settings ? settings.value("workspace.settingsPage") : ""))
        pageList.currentIndex = shown >= 0 ? shown : 0
        if (filter !== undefined) {
            hotkeyFilter.currentIndex = filter
            hotkeys.filter(hotkeyFilter.currentIndex)
        }
        open()
        rememberPage() // a deep link's page is the one shown last
        // NT1 (#302): the legacy constructor's "does not exist" message
        // boxes, one at a time; warning toasts now.
        if (notices)
            for (const warning of r.warnings)
                notices.post("warning", qsTr("Settings"), warning)
    }
    function rememberPage() {
        if (visible && settings && pageList.currentIndex >= 0)
            settings.setValue("workspace.settingsPage", pages[pageList.currentIndex].id)
    }
    // O5: Apply switched the interface language: the dictionary choice's
    // entry for an empty Dictionary folder in the new language.
    Connections {
        target: dialog.app
        function onSettingsListsChanged(dictionaries) { dialog.dictionaries = dictionaries }
    }
    // AD2: asks first; OK or Apply forgets the answers (and Repeat's record).
    function clearLastAnswers() {
        ask({
            title: qsTr("Clear last answers"),
            text: qsTr("Forget the answers given in every script dialog?"),
            detail: qsTr("Repeat last run is off until a macro runs again. Presets are kept."),
            severity: "warning",
            choices: [{ id: "clear", text: qsTr("Clear"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => { if (id === "clear") put("automation.dialogAnswers", []) })
    }
    // PK1's message box, made at its first question.
    readonly property var question: questionLoader.item
    function ask(options, done) {
        questionLoader.active = true
        questionLoader.item.ask(options, done)
    }
    function put(setting, value) {
        const v = Object.assign({}, values)
        v[setting] = value
        values = v
        // K2: the appearance shows at once; OK or Apply saves it, Cancel takes it back.
        if (setting.startsWith("appearance."))
            Theme.preview(values)
    }
    // The Appearance page's colour picker for a high-contrast colour (SY2:
    // the colour popover at `anchor`). Each change is staged at once as
    // "#RRGGBB" and previewed; Esc puts the colour before back.
    function pickThemeColour(setting, colour, anchor, name) {
        themeColourPopover.openFor(anchor, themeColourTarget, () => {
            // The staged colour once the one open before has ended.
            const now = values[setting] ?? colour
            themeColourTarget.setting = setting
            themeColourTarget.before = now
            colourPicker.opened("settings")
            const c = colourPicker.htmlColour(now)
            return { r: c.r, g: c.g, b: c.b, a: 0 }
        }, { title: name, alpha: false, focus: anchor })
    }
    // Apply keeps the controls as they are (legacy does not refresh them).
    function apply() {
        app.applySettings(values, resetAllStaged)
        resetAllStaged = false
        commitTagButtons()
    }
    // "Reset page": the page's settings at their defaults, staged; the
    // Hotkeys page's every default hotkey, in the list's history.
    function resetPage() {
        if (currentPage === "hotkeys") {
            hotkeys.optionsResetAll()
            return
        }
        values = app.resetSettingsPage(values, currentPage)
        if (currentPage === "appearance")
            Theme.preview(values)
        if (currentPage === "lineEditor")
            tagButtonList = [] // the page's tag buttons too, staged
        if (currentPage === "conversion")
            styles = app.settingsStyles() // the default catalog's
        reloaded()
    }
    // "Reset all settings…", after its question: every page but the
    // appearance and the hotkeys at the defaults, staged.
    function resetAll() {
        ask({
            title: qsTr("Reset all settings"),
            text: qsTr("Reset all settings to their defaults?"),
            detail: qsTr("Every page goes back to its defaults when you press OK or Apply; Cancel keeps your settings. Hotkeys and the appearance stay as they are: reset them on their own pages."),
            severity: "warning",
            choices: [
                { id: "reset", text: qsTr("Reset all"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, answer => {
            if (answer !== "reset")
                return
            dialog.values = dialog.app.resetSettings(dialog.values)
            dialog.resetAllStaged = true
            // The shown tag buttons stay (EditBox keeps what SetTagButtons built).
            dialog.copyTagButtons()
            dialog.reloaded()
        })
    }
    function copyTagButtons() {
        tagButtonList = tagButtons ? tagButtons.buttons.map(b => Object.assign({}, b)) : []
    }
    function commitTagButtons() {
        if (!tagButtons)
            return
        const shown = tagButtons.buttons
        const same = shown.length === tagButtonList.length && shown.every((b, i) => {
            const t = tagButtonList[i]
            return b.name === t.name && b.tag === t.tag && b.type === t.type && b.icon === t.icon
        })
        if (!same)
            tagButtons.replace(tagButtonList)
    }
    function stageTagButton(index, b) {
        const list = tagButtonList.slice()
        if (index < 0)
            list.push(b)
        else
            list[index] = b
        tagButtonList = list
        tagButtonRows.currentIndex = index < 0 ? list.length - 1 : index
    }
    function removeTagButton(index) {
        const list = tagButtonList.slice()
        list.splice(index, 1)
        tagButtonList = list
        tagButtonRows.currentIndex = Math.min(index, list.length - 1)
    }
    function moveTagButton(index, step) {
        const list = tagButtonList.slice()
        const b = list[index]
        list[index] = list[index + step]
        list[index + step] = b
        tagButtonList = list
        tagButtonRows.currentIndex = index + step
    }
    // O2: OnMapHkey for the selected row: the mapping window, then the
    // question when the keys are taken (ItemHotkey::OnMapHotkey).
    readonly property color warningColour: Theme.warning // WINDOW_WARNING_ELEMENTS
    // PK1: the capture as a popover under the row (or under `from`).
    function mapHotkey(from) {
        const target = hotkeys.mapTarget()
        if (target.name === undefined || hotkeys.mapProblem.length > 0)
            return
        const row = hotkeyList.itemAtIndex(hotkeyList.selectedRow)
        settingsHotkeyMapping.capture(target.name, target.type, target.windows, (accel, window) => {
            const conflict = hotkeys.optionsConflict(accel, window)
            if (conflict.message === undefined)
                hotkeys.optionsMap(accel, window, "cancel")
            else
                settingsHotkeyMapping.ask(conflict, answer => hotkeys.optionsMap(accel, window, answer))
        }, { anchor: row && row.visible ? row : from, clear: () => hotkeys.optionsDelete() })
    }
    function chooseCatalog(index) {
        const r = app.chooseSettingsCatalog(values, index)
        styles = r.styles
        // Over the staged values: the ones without a control (Ask again's
        // notices.hiddenQuestions) stay staged (Codex review).
        values = Object.assign({}, values, r.values)
        reloaded()
    }

    onAccepted: {
        app.applySettings(values, resetAllStaged)
        resetAllStaged = false
        commitTagButtons()
    }
    // K2: the preview ends with the window: after OK or Apply the saved
    // appearance shows, after Cancel the one before.
    onVisibleChanged: if (!visible) Theme.endPreview()

    // "Reset page" at the leading edge (its menu: "Reset all settings…");
    // OK, Cancel and Apply at the trailing one in the platform's order.
    leadingActions: [
        RowLayout {
            spacing: 2
            IconTextButton {
                objectName: "settingsResetPage"
                iconRole: "restore-default"
                text: qsTr("Reset page")
                tip: qsTr("The settings of the page shown back to their defaults")
                onClicked: dialog.resetPage()
            }
            IconToolButton {
                id: resetMenuButton
                objectName: "settingsResetMenu"
                iconRole: "chevron-down"
                text: qsTr("More resets")
                onClicked: resetMenu.popup(resetMenuButton, 0, resetMenuButton.height)
            }
            ShellMenu {
                id: resetMenu
                ShellMenuItem {
                    objectName: "settingsResetThisPage"
                    iconRole: "restore-default"
                    text: qsTr("Reset this page")
                    onTriggered: dialog.resetPage()
                }
                ShellMenuItem {
                    objectName: "settingsResetAll"
                    iconRole: "settings"
                    text: qsTr("Reset all settings…")
                    onTriggered: dialog.resetAll()
                }
            }
        }
    ]
    buttons: [
        PrimaryButton {
            id: okButton
            objectName: "settingsOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        },
        Button {
            objectName: "settingsCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        },
        Button {
            objectName: "settingsApply"
            text: qsTr("Apply")
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: dialog.apply()
        }
    ]

    // A page: a scrolling column of sections inside the page's margins.
    component SettingsPage: ScrollView {
        id: settingsPage
        default property alias content: pageColumn.data
        contentWidth: availableWidth
        contentHeight: pageColumn.implicitHeight + 44
        ColumnLayout {
            id: pageColumn
            x: 24
            y: 20
            width: settingsPage.availableWidth - 48
            spacing: 16
        }
    }
    // A muted line under a check box: when its change takes effect, what
    // it means.
    component SettingNote: Label {
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.leftMargin: 24
        Layout.topMargin: -5
        Layout.fillWidth: true
    }
    // A check box bound to a boolean setting, its box in the rows' icon
    // slot. Its label wraps in the page's width.
    component SettingCheck: CheckBox {
        id: check
        required property string setting
        objectName: "setting_" + setting
        leftPadding: 1
        spacing: 9
        Layout.fillWidth: text.length > 0
        Layout.preferredHeight: Math.max(implicitIndicatorHeight, contentItem.implicitHeight) + topPadding + bottomPadding
        Accessible.name: text
        function refresh() { checked = dialog.values[setting] === true }
        Component.onCompleted: {
            if (contentItem && contentItem.wrapMode !== undefined)
                contentItem.wrapMode = Text.Wrap
            refresh()
        }
        Connections { target: dialog; function onReloaded() { check.refresh() } }
        onToggled: dialog.put(setting, checked)
    }
    // A NumCtrl: a number within the legacy range, its unit after it.
    component SettingNumber: RowLayout {
        id: numberRow
        required property string setting
        property string unit
        property string name // the accessible name
        property string tip
        property int from: 0
        property int to: 100000
        spacing: 6
        SpinBox {
            id: spin
            objectName: "setting_" + numberRow.setting
            from: numberRow.from
            to: numberRow.to
            editable: true
            Accessible.name: numberRow.name
            Layout.preferredWidth: 120
            ToolTip.text: numberRow.tip
            ToolTip.visible: hovered && numberRow.tip !== ""
            function refresh() { value = dialog.values[numberRow.setting] ?? 0 }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { spin.refresh() } }
            onValueModified: dialog.put(numberRow.setting, value)
        }
        Label {
            visible: numberRow.unit.length > 0
            text: numberRow.unit
            color: Theme.muted
        }
    }
    // A text field bound to a string setting.
    component SettingText: TextField {
        id: field
        required property string setting
        objectName: "setting_" + setting
        Layout.preferredWidth: 260
        function refresh() { text = dialog.values[setting] ?? "" }
        Component.onCompleted: refresh()
        Connections { target: dialog; function onReloaded() { field.refresh() } }
        onTextEdited: dialog.put(setting, text)
    }
    // A HikariChoice: `values` holds its selection, which may be -1 (nothing
    // shown) or past the list; it changes only when an entry is chosen.
    component SettingChoice: ComboBox {
        id: choice
        required property string setting
        objectName: "setting_" + setting
        Layout.preferredWidth: 260
        signal chosen(int index)
        function refresh() {
            const v = dialog.values[setting] ?? -1
            currentIndex = v >= 0 && v < count ? v : -1
        }
        Component.onCompleted: refresh()
        onModelChanged: refresh()
        onCountChanged: refresh()
        Connections { target: dialog; function onReloaded() { choice.refresh() } }
        onActivated: index => {
            dialog.put(setting, index)
            chosen(index)
        }
    }
    // FontPickerButton: the family and point size of a font setting.
    component SettingFont: RowLayout {
        id: fontRow
        required property string setting
        required property string sizeSetting
        property string name // the accessible name
        spacing: 8
        ComboBox {
            id: family
            objectName: "setting_" + fontRow.setting
            editable: true
            model: Qt.fontFamilies()
            Accessible.name: fontRow.name
            Layout.preferredWidth: 200
            function refresh() { editText = dialog.values[fontRow.setting] ?? "" }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { family.refresh() } }
            onActivated: dialog.put(fontRow.setting, currentText)
            onAccepted: dialog.put(fontRow.setting, editText)
        }
        SpinBox {
            id: size
            objectName: "setting_" + fontRow.sizeSetting
            from: 1
            to: 200
            editable: true
            Accessible.name: qsTr("Font size")
            function refresh() { value = dialog.values[fontRow.sizeSetting] ?? 10 }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { size.refresh() } }
            onValueModified: dialog.put(fontRow.sizeSetting, value)
        }
    }

    ColumnLayout {
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
        RowLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            // MuseScore 4's reference size, the window's smallest.
            Layout.preferredWidth: 880
            Layout.preferredHeight: 528

            // The page list on the background, the page on the panel.
            Rectangle {
                color: Theme.background
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                ListView {
                    id: pageList
                    objectName: "settingsPages"
                    anchors.fill: parent
                    anchors.rightMargin: 1
                    topMargin: 8
                    clip: true
                    model: dialog.pages
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: dialog.title
                    // The page shown last, where the window opens next.
                    onCurrentIndexChanged: dialog.rememberPage()
                    delegate: ItemDelegate {
                        id: pageItem
                        required property var modelData
                        required property int index
                        objectName: "settingsPage_" + modelData.id
                        width: ListView.view.width
                        height: 36
                        highlighted: ListView.isCurrentItem
                        text: modelData.name
                        leftPadding: 16
                        rightPadding: 12
                        focusPolicy: Qt.NoFocus
                        onClicked: pageList.currentIndex = index
                        Accessible.role: Accessible.PageTab
                        Accessible.name: text
                        Accessible.selected: highlighted
                        // The selection's fill, the accent marker and bold (MuseScore's selected page).
                        background: Rectangle {
                            color: pageItem.highlighted ? Theme.select : pageItem.hovered ? Theme.raised : "transparent"
                            Rectangle {
                                visible: pageItem.highlighted
                                width: 3
                                height: parent.height
                                color: Theme.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Icon { objectName: "settingsPageIcon"; iconRole: pageItem.modelData.iconRole }
                            Label {
                                text: pageItem.text
                                font.bold: pageItem.highlighted
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.line
                }
            }
            StackLayout {
                id: pageStack
                objectName: "settingsPageStack"
                currentIndex: pageList.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true

                // General: the language, tabs, saving, notices, folders.
                SettingsPage {
                    objectName: "settingsPageGeneral"
                    SettingsSection {
                        objectName: "settingsLanguageGroup"
                        title: qsTr("Language")
                        ruled: false
                        // Legacy "Language (program restart required)": the
                        // switch is live (O5-language-label).
                        SettingsRow {
                            iconRole: "language"
                            text: qsTr("Interface language")
                            SettingChoice {
                                setting: "program.language"
                                model: dialog.languages
                                Accessible.name: qsTr("Interface language")
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Tabs")
                        SettingCheck { setting: "subtitles.openInNewTab"; text: qsTr("Always open subtitles in a new tab") }
                        SettingCheck { setting: "grid.loadSortedSubs"; text: qsTr("Open sorted subtitles") }
                        SettingCheck { setting: "shiftTimes.changeValuesWithTab"; text: qsTr("Synchronize time shifting window in all tabs") }
                        SettingsRow {
                            text: qsTr("Tab name length")
                            SettingNumber {
                                setting: "program.tabTextMaxChars"; from: 20; to: 150; unit: qsTr("characters")
                                name: qsTr("Tab name length"); tip: qsTr("Number of tab name characters. Range from 20 to 150")
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Saving")
                        SettingsRow {
                            iconRole: "autosave"
                            text: qsTr("Save while editing every")
                            note: qsTr("0 turns saving while editing off.")
                            SettingNumber { setting: "editor.saveAfterCharacterCount"; from: 0; to: 10000; unit: qsTr("edits"); name: qsTr("Save while editing every") }
                        }
                        SettingsRow {
                            text: qsTr("Autosave files kept")
                            SettingNumber {
                                setting: "autosave.maxFiles"; from: 2; to: 1000000; name: qsTr("Autosave files kept")
                                tip: qsTr("Number of autosaves can be set from 2 to 1000000")
                            }
                        }
                    }
                    // PK1 (#301): notices as toasts or in the Log only, and
                    // the questions hidden with "Don't ask again" back.
                    SettingsSection {
                        objectName: "settingsNoticesGroup"
                        title: qsTr("Notices")
                        SettingCheck { setting: "notices.toasts"; text: qsTr("Show notices as toasts") }
                        SettingNote { text: qsTr("Off: notices go to the Log only.") }
                        SettingsRow {
                            iconRole: "notices"
                            text: qsTr("Questions hidden with “Don’t ask again”")
                            Button {
                                objectName: "settingsAskAgain"
                                text: qsTr("Ask again")
                                enabled: (dialog.values["notices.hiddenQuestions"] || []).length > 0
                                onClicked: dialog.put("notices.hiddenQuestions", [])
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Folders")
                        SettingsRow {
                            iconRole: "folder-open"
                            text: qsTr("Fonts folder")
                            SettingText { id: fontsFolder; setting: "fonts.externalDirectory"; Accessible.name: qsTr("Fonts folder") }
                            Button {
                                text: qsTr("Choose…")
                                Accessible.name: qsTr("Choose external font folder")
                                onClicked: fontsFolderDialog.open()
                            }
                        }
                    }
                }

                // K2: Appearance, in the Themes page's place.
                AppearancePage { dialog: dialog }

                // Line editor: spelling, display, editing, Translation mode, LE5's tag buttons.
                SettingsPage {
                    objectName: "settingsPageLineEditor"
                    SettingsSection {
                        title: qsTr("Spelling")
                        ruled: false
                        SettingCheck { setting: "editor.spellchecker"; text: qsTr("Check spelling") }
                        SettingsRow {
                            iconRole: "dictionary"
                            text: qsTr("Dictionary")
                            SettingChoice {
                                setting: "editor.dictionaryLanguage"
                                model: dialog.dictionaries
                                // With no dictionary installed, a disabled box
                                // that says so; with none chosen, it says that
                                // (not a blank box).
                                enabled: count > 0
                                displayText: currentIndex >= 0 ? currentText
                                           : count > 0 ? qsTr("None chosen") : qsTr("No dictionaries found")
                                Accessible.name: qsTr("Dictionary")
                            }
                        }
                        SettingCheck { setting: "editor.suggestionsOnDoubleClick"; text: qsTr("Show suggestions on double-click") }
                    }
                    SettingsSection {
                        title: qsTr("Display")
                        // LE3-tags-shown: the Line editor shows ASS override tags unless this is on
                        // (Subtitles > "Hide tags in the line editor" switches it too).
                        SettingCheck { setting: "editor.hideTags"; text: qsTr("Hide tags") }
                        // LE2: legacy's Time / Frames radios under the Line editor
                        // (EDITBOX_TIMES_TO_FRAMES_SWITCH); the Video panel's "⋯" menu has it too.
                        SettingCheck { setting: "editor.timesToFramesSwitch"; text: qsTr("Show video frames instead of times") }
                        SettingNote { text: qsTr("In the line editor and the subtitle grid.") }
                    }
                    SettingsSection {
                        title: qsTr("Editing")
                        SettingCheck { setting: "editor.dontGoToNextLineOnTimesEdit"; text: qsTr("Stay on the line when editing times") }
                        SettingCheck { setting: "editor.allowNumpadHotkeys"; text: qsTr("Use numpad shortcuts in text fields") }
                    }
                    SettingsSection {
                        title: qsTr("Translation mode")
                        SettingCheck { setting: "translation.showOriginal"; text: qsTr("Show the original") }
                        SettingCheck { setting: "translation.hideOriginalOnVideo"; text: qsTr("Hide the original on the video") }
                    }
                    // LE5: the Line editor's custom tag buttons, in strip order.
                    SettingsSection {
                        objectName: "settingsTagButtons"
                        title: qsTr("Tag buttons")
                        visible: dialog.tagButtons !== null
                        ColumnLayout {
                            spacing: 4
                            Layout.leftMargin: 24
                            Layout.fillWidth: true
                            ListView {
                                id: tagButtonRows
                                objectName: "settingsTagButtonList"
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(Math.max(count, 2), 6) * 28
                                clip: true
                                model: dialog.tagButtonList
                                currentIndex: -1
                                keyNavigationEnabled: true
                                activeFocusOnTab: true
                                Accessible.name: qsTr("Tag buttons")
                                ScrollBar.vertical: ScrollBar {}
                                delegate: ItemDelegate {
                                    id: row
                                    required property var modelData
                                    required property int index
                                    objectName: "settingsTagButton" + index
                                    width: ListView.view.width
                                    height: 28
                                    highlighted: ListView.isCurrentItem
                                    Accessible.name: modelData.name.length > 0 ? modelData.name : modelData.tag
                                    onClicked: tagButtonRows.currentIndex = index
                                    onDoubleClicked: tagButtonEditor.openFor(editTagButton, index, modelData)
                                    background: Rectangle {
                                        color: row.highlighted ? Theme.select : "transparent"
                                        Rectangle { visible: row.highlighted; width: 3; height: parent.height; color: Theme.accent }
                                    }
                                    contentItem: RowLayout {
                                        spacing: 8
                                        Icon {
                                            iconRole: row.modelData.icon
                                            opacity: row.modelData.icon.length > 0 ? 1 : 0
                                        }
                                        Label {
                                            text: row.modelData.name
                                            color: Theme.text
                                            Layout.preferredWidth: 120
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: row.modelData.tag
                                            color: Theme.muted
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                Label {
                                    parent: tagButtonRows // over the view, not in its content
                                    anchors.centerIn: parent
                                    visible: tagButtonRows.count === 0
                                    text: qsTr("No tag buttons")
                                    color: Theme.muted
                                }
                                Rectangle {
                                    parent: tagButtonRows
                                    anchors.fill: parent
                                    z: -1
                                    color: Theme.field
                                    border.color: Theme.line
                                }
                            }
                            RowLayout {
                                spacing: 2
                                readonly property int current: tagButtonRows.currentIndex
                                readonly property bool chosen: current >= 0 && current < tagButtonRows.count
                                IconToolButton {
                                    id: addTagButton
                                    objectName: "settingsAddTagButton"
                                    iconRole: "add"
                                    text: qsTr("Add a tag button")
                                    enabled: tagButtonRows.count < (dialog.tagButtons ? dialog.tagButtons.maximum : 0)
                                    onClicked: tagButtonEditor.openFor(addTagButton, -1, null)
                                }
                                IconToolButton {
                                    id: editTagButton
                                    objectName: "settingsEditTagButton"
                                    iconRole: "edit"
                                    text: qsTr("Edit the tag button")
                                    enabled: parent.chosen
                                    onClicked: tagButtonEditor.openFor(editTagButton, parent.current, dialog.tagButtonList[parent.current])
                                }
                                IconToolButton {
                                    objectName: "settingsMoveTagButtonUp"
                                    iconRole: "move-up"
                                    text: qsTr("Move up")
                                    enabled: parent.chosen && parent.current > 0
                                    onClicked: dialog.moveTagButton(parent.current, -1)
                                }
                                IconToolButton {
                                    objectName: "settingsMoveTagButtonDown"
                                    iconRole: "move-down"
                                    text: qsTr("Move down")
                                    enabled: parent.chosen && parent.current < tagButtonRows.count - 1
                                    onClicked: dialog.moveTagButton(parent.current, 1)
                                }
                                IconToolButton {
                                    objectName: "settingsRemoveTagButton"
                                    iconRole: "remove"
                                    text: qsTr("Remove the tag button")
                                    enabled: parent.chosen
                                    onClicked: dialog.removeTagButton(parent.current)
                                }
                            }
                            TagButtonEditor {
                                id: tagButtonEditor
                                objectName: "settingsTagButtonEditor"
                                tagButtons: dialog.tagButtons
                                onSaved: (index, b) => dialog.stageTagButton(index, b)
                            }
                        }
                    }
                }

                // Subtitle grid: the Grid's settings from legacy Editor and Editor > Advanced.
                SettingsPage {
                    objectName: "settingsPageGrid"
                    SettingsSection {
                        title: qsTr("Font")
                        ruled: false
                        SettingsRow {
                            iconRole: "tag-font"
                            text: qsTr("Grid font")
                            SettingFont { setting: "grid.font"; sizeSetting: "grid.fontSize"; name: qsTr("Grid font") }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Active line")
                        SettingCheck { setting: "grid.autoSelectLinesFromLastTab"; text: qsTr("Select the line at the previous tab's time") }
                        SettingCheck { setting: "grid.changeActiveOnSelection"; text: qsTr("Change the active line when adding to the selection") }
                        SettingCheck { setting: "grid.duplicationDontChangeSelection"; text: qsTr("Keep the selection when duplicating lines") }
                        SettingCheck { setting: "grid.dontCenterActiveLine"; text: qsTr("Don't center the active line") }
                        SettingCheck { setting: "grid.setVisibleLineAfterFullScreen"; text: qsTr("Find the visible line after leaving full screen") }
                    }
                    SettingsSection {
                        title: qsTr("Columns")
                        // GR3: the Grid's Hide columns > Hide empty columns, one setting.
                        SettingCheck { setting: "grid.autoHideColumns"; text: qsTr("Hide empty columns") }
                    }
                    SettingsSection {
                        title: qsTr("Counting")
                        SettingCheck { setting: "grid.calcSpacesAndPunctuationForWraps"; text: qsTr("Count spaces and punctuation for wraps") }
                        SettingCheck { setting: "grid.calcSpacesAndPunctuationForCps"; text: qsTr("Count spaces and punctuation for CPS") }
                    }
                    SettingsSection {
                        title: qsTr("New lines")
                        SettingsRow {
                            text: qsTr("Start frame offset")
                            SettingNumber { setting: "grid.insertStartOffset"; from: -100000; to: 100000; unit: qsTr("ms"); name: qsTr("Start frame offset") }
                        }
                        SettingsRow {
                            text: qsTr("End frame offset")
                            SettingNumber { setting: "grid.insertEndOffset"; from: -100000; to: 100000; unit: qsTr("ms"); name: qsTr("End frame offset") }
                        }
                        SettingsRow {
                            text: qsTr("ASS tag replacement")
                            SettingText { setting: "grid.tagsSwapCharacter"; Layout.preferredWidth: 120; Accessible.name: qsTr("ASS tag replacement") }
                        }
                    }
                }

                // Video (legacy video page), with the Video settings legacy kept on Main.
                SettingsPage {
                    objectName: "settingsPageVideo"
                    SettingsSection {
                        title: qsTr("Playback")
                        ruled: false
                        SettingCheck { setting: "video.pauseOnClick"; text: qsTr("Left mouse button pauses video") }
                        SettingCheck { setting: "video.fullScreenOnStart"; text: qsTr("Open video from the context menu in full screen") }
                        SettingCheck { setting: "video.openAtActiveLine"; text: qsTr("Open video at the active line's time") }
                        // VF1 (#288): the Video panel's two follow lists, also in
                        // its menu ("Seek on", "Then play"): OK and Apply store
                        // them as the other pages', and the menu follows.
                        SettingsRow {
                            text: dialog.follow.seekName
                            SettingChoice {
                                setting: "video.moveToActiveLine"
                                model: dialog.follow.seekChoices
                                Accessible.name: dialog.follow.seekLabel
                            }
                        }
                        SettingsRow {
                            text: dialog.follow.playName
                            SettingChoice {
                                setting: "video.playAfterSelection"
                                model: dialog.follow.playChoices
                                Accessible.name: dialog.follow.playLabel
                            }
                        }
                        // W1 (Windows): the player that plays video, the
                        // built-in one or the optional DirectShow adapter
                        // (legacy's player with indexing off). It applies at
                        // once; a playing video pauses.
                        Loader {
                            // DX1: the docs export's entry on every platform.
                            readonly property var docsEntries: [{
                                setting: "video.playbackPlayer", control: "choice", label: qsTr("Player"), platform: "windows",
                                values: [{value: 0, label: qsTr("Built-in")}, {value: 1, label: "DirectShow"}]
                            }]
                            active: Qt.platform.os === "windows"
                            visible: active
                            Layout.fillWidth: true
                            sourceComponent: SettingsRow {
                                objectName: "videoPlaybackPlayerBox"
                                text: qsTr("Player")
                                SettingChoice {
                                    setting: "video.playbackPlayer"
                                    model: [qsTr("Built-in"), "DirectShow"]
                                    Accessible.name: qsTr("Player")
                                }
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Subtitles on video")
                        // W2: legacy's "Subtitle display filter" (ID_VSFILTER_PROVIDER),
                        // only when there is a CSRI renderer to choose besides libass
                        // (W2-filter-row-hidden).
                        SettingsRow {
                            objectName: "settingsRendererGroup"
                            visible: dialog.renderers.length > 1
                            iconRole: "renderer"
                            text: qsTr("Subtitle display filter")
                            SettingChoice {
                                setting: "video.subtitleProvider"
                                model: dialog.rendererLabels
                                Accessible.name: qsTr("Subtitle display filter")
                            }
                        }
                        // SV1 (#365): where libass draws the previews'
                        // subtitles; a CSRI filter draws at the video's size.
                        SettingsRow {
                            id: previewAtRow
                            objectName: "settingsPreviewSubtitlesAt"
                            readonly property bool libass: dialog.renderers.length < 2
                                || dialog.renderers[dialog.values["video.subtitleProvider"] ?? -1] === "libass"
                            text: qsTr("Preview subtitles at")
                            note: libass ? qsTr("Screen size draws subtitles at the size the picture takes on screen, so they stay sharp on large and high-DPI screens. The script's layout is the same either way. Also used by the previews in the Style manager, the font picker and the Template editor.")
                                         : qsTr("Video resolution only with this filter.")
                            SettingChoice {
                                setting: "video.previewSubtitlesAt"
                                enabled: previewAtRow.libass
                                model: [qsTr("Video resolution (as encoded)"), qsTr("Screen size (sharp)")]
                                Accessible.name: qsTr("Preview subtitles at")
                            }
                        }
                        SettingCheck { setting: "video.disableLiveEditing"; text: qsTr("Turn off the edits preview") }
                        SettingNote { text: qsTr("Applies when the tab opens again.") }
                    }
                    SettingsSection {
                        title: qsTr("Decoding")
                        SettingsRow {
                            iconRole: "video-seek"
                            text: qsTr("Seeking method")
                            note: qsTr("Applies when the video opens again.")
                            SettingChoice {
                                setting: "video.ffms2Seeking"
                                model: [qsTr("Linear"), qsTr("Normal"), qsTr("Unsafe (always fast)"), qsTr("Aggressive (fast in rewind)")]
                                Accessible.name: qsTr("Seeking method")
                            }
                        }
                        SettingCheck { setting: "video.gpuConversion"; text: qsTr("Convert video colours on the GPU") }
                        SettingNote { text: qsTr("Applies when the video opens again.") }
                        SettingsRow {
                            text: qsTr("Preferred audio")
                            note: qsTr("Separated by semicolons.")
                            SettingText { setting: "video.acceptedAudioStream"; Accessible.name: qsTr("Preferred audio") }
                        }
                    }
                    SettingsSection {
                        title: qsTr("View")
                        SettingsRow {
                            iconRole: "zoom"
                            text: qsTr("Start zoom")
                            // A NumCtrl with its own id: OK stores its digits as typed.
                            SettingText {
                                setting: "video.zoomPercent"
                                Layout.preferredWidth: 120
                                maximumLength: 20
                                validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
                                Accessible.name: qsTr("Start zoom")
                            }
                            Label { text: "%"; color: Theme.muted }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Script resolution")
                        SettingCheck { setting: "video.visualWarningsOff"; text: qsTr("Turn off the visual tools warning") }
                        SettingCheck { setting: "video.dontAskForBadResolution"; text: qsTr("Don't warn about a resolution mismatch") }
                        // Y4-playres-ask: the script resolution question's "Always" choice.
                        SettingCheck { setting: "automation.scriptResolutionFromVideo"; text: qsTr("Set a missing script resolution from the video without asking") }
                    }
                }

                // Audio (legacy AudioMain and AudioSecond).
                SettingsPage {
                    objectName: "settingsPageAudio"
                    // H2-output-device: the device both the audio box and
                    // video playback play through (System default follows
                    // the system's default); A4-wasapi-default (Windows): the
                    // audio box's output API, which applies when the output
                    // next opens.
                    SettingsSection {
                        objectName: "audioOutputBox"
                        title: qsTr("Output")
                        ruled: false
                        SettingsRow {
                            iconRole: "audio-device"
                            text: qsTr("Device")
                            SettingChoice {
                                setting: "audio.outputDevice"
                                model: dialog.outputDevices
                                Accessible.name: qsTr("Device")
                            }
                        }
                        Loader {
                            objectName: "audioOutputHostApiBox"
                            // DX1: the docs export's entry on every platform.
                            readonly property var docsEntries: [{
                                setting: "audio.outputHostApi", control: "choice", label: qsTr("API"), platform: "windows",
                                hint: qsTr("Applies when the output opens again."),
                                values: [{value: 0, label: "WASAPI"}, {value: 1, label: "DirectSound"}]
                            }]
                            active: Qt.platform.os === "windows"
                            visible: active
                            Layout.fillWidth: true
                            sourceComponent: SettingsRow {
                                text: qsTr("API")
                                note: qsTr("Applies when the output opens again.")
                                SettingChoice {
                                    setting: "audio.outputHostApi"
                                    model: ["WASAPI", "DirectSound"]
                                    Accessible.name: qsTr("API")
                                }
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Playback")
                        SettingsRow {
                            iconRole: "audio-delay"
                            text: qsTr("Audio delay")
                            SettingNumber { setting: "audio.delay"; from: -50000000; to: 50000000; unit: qsTr("ms"); name: qsTr("Audio delay") }
                        }
                        SettingsRow {
                            text: qsTr("Play around the marker")
                            SettingNumber { setting: "audio.markPlayTime"; from: 400; to: 5000; unit: qsTr("ms"); name: qsTr("Play around the marker") }
                        }
                        SettingsRow {
                            iconRole: "lead-in"
                            text: qsTr("Lead-in and lead-out")
                            SettingNumber { setting: "audio.leadInValue"; from: 0; to: 10000; unit: qsTr("ms"); name: qsTr("Lead-in") }
                            SettingNumber { setting: "audio.leadOutValue"; from: 0; to: 10000; unit: qsTr("ms"); name: qsTr("Lead-out") }
                        }
                        SettingCheck { setting: "audio.lockScrollOnCursor"; text: qsTr("Follow audio during playback") }
                        SettingCheck { setting: "audio.dontPlayWhenLineChanges"; text: qsTr("Don't play after changing the line") }
                        SettingCheck { setting: "audio.autoFocus"; text: qsTr("Hotkeys follow the panel under the pointer") } // KB1
                    }
                    SettingsSection {
                        title: qsTr("Display")
                        SettingCheck { setting: "audio.drawTimeCursor"; text: qsTr("Show the time beside the cursor") }
                        SettingCheck { setting: "audio.drawSecondaryLines"; text: qsTr("Show seconds markers") }
                        SettingCheck { setting: "audio.drawSelectionBackground"; text: qsTr("Show the selection background") }
                        SettingCheck { setting: "audio.drawVideoPosition"; text: qsTr("Show the video position") }
                        SettingCheck { setting: "audio.drawKeyframes"; text: qsTr("Show keyframes") }
                        SettingsRow {
                            text: qsTr("Line boundaries thickness")
                            SettingNumber { setting: "audio.lineBoundariesThickness"; from: 1; to: 5; name: qsTr("Line boundaries thickness") }
                        }
                        SettingsRow {
                            text: qsTr("Inactive lines")
                            SettingChoice {
                                setting: "audio.inactiveLinesDisplayMode"
                                model: [qsTr("None"), qsTr("Before and after the active"), qsTr("All visible")]
                                Accessible.name: qsTr("Inactive lines")
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Snapping")
                        SettingCheck { setting: "audio.snapToKeyframes"; text: qsTr("Snap to keyframes") }
                        SettingCheck { setting: "audio.snapToOtherLines"; text: qsTr("Snap to other lines") }
                    }
                    SettingsSection {
                        title: qsTr("Karaoke")
                        SettingCheck { setting: "audio.mergeEveryNWithSyllable"; text: qsTr("Merge every \"n\" with the previous syllable") }
                        SettingCheck { setting: "audio.karaokeMoveOnClick"; text: qsTr("Move the syllable line after a click") }
                        // KA1 (#276): Auto split's double consonants, and the
                        // length the \k and \kf switch takes as long
                        SettingCheck { setting: "audio.karaokeDoubleConsonantApart"; text: qsTr("Give a double consonant its own syllable (so|ma|t|ta)") }
                        SettingsRow {
                            text: qsTr("Long syllables for %1").arg("\\kf")
                            SettingNumber {
                                setting: "audio.karaokeKfThreshold"; from: 10; to: 10000; unit: qsTr("cs"); name: qsTr("Long syllables for %1").arg("\\kf")
                                tip: qsTr("Switch long syllables between %1 and %2 changes the syllables at least this long.").arg("\\k").arg("\\kf")
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Cache")
                        SettingCheck { setting: "audio.ramCache"; text: qsTr("Load audio into RAM") }
                        SettingsRow {
                            text: qsTr("Cache files kept")
                            note: qsTr("0 keeps them all.")
                            SettingNumber {
                                setting: "audio.cacheFilesLimit"; from: 0; to: 10000; unit: qsTr("files"); name: qsTr("Cache files kept")
                                tip: qsTr("Range from 0 to 10000, where 0 turns off\nremoving audio cache files.")
                            }
                        }
                    }
                }

                // Conversion (legacy Editor > Conversion, ConvOpt).
                SettingsPage {
                    objectName: "settingsPageConversion"
                    SettingsSection {
                        title: qsTr("Style")
                        ruled: false
                        SettingsRow {
                            iconRole: "styles"
                            text: qsTr("Catalog")
                            SettingChoice {
                                setting: "convert.styleCatalog"
                                model: dialog.catalogs
                                Accessible.name: qsTr("Catalog")
                                onChosen: index => dialog.chooseCatalog(index) // OnChangeCatalog
                            }
                        }
                        SettingsRow {
                            text: qsTr("Style")
                            SettingChoice {
                                setting: "convert.style"
                                model: dialog.styles
                                Accessible.name: qsTr("Style")
                            }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Timing")
                        SettingsRow {
                            iconRole: "fps"
                            text: qsTr("Frame rate")
                            ComboBox {
                                id: fpsBox
                                objectName: "setting_convert.fps"
                                editable: true
                                model: ["23.976", "24", "25", "29.97", "30", "60"]
                                Accessible.name: qsTr("Frame rate")
                                Layout.preferredWidth: 120
                                function refresh() { editText = dialog.values["convert.fps"] ?? "" }
                                Component.onCompleted: refresh()
                                Connections { target: dialog; function onReloaded() { fpsBox.refresh() } }
                                onActivated: dialog.put("convert.fps", currentText)
                                onEditTextChanged: if (activeFocus) dialog.put("convert.fps", editText)
                            }
                        }
                        SettingCheck { setting: "convert.fpsFromVideo"; text: qsTr("Frame rate from the video") }
                        SettingCheck { setting: "convert.newEndTimes"; text: qsTr("New end times") }
                        SettingsRow {
                            text: qsTr("Time per letter")
                            SettingNumber { setting: "convert.timePerCharacter"; from: 30; to: 1000; unit: qsTr("ms"); name: qsTr("Time per letter") }
                        }
                    }
                    SettingsSection {
                        title: qsTr("ASS")
                        SettingsRow {
                            iconRole: "resample"
                            text: qsTr("Resolution")
                            SettingNumber { setting: "convert.resolutionWidth"; from: 1; to: 3000; name: qsTr("Width") }
                            Label { text: "×"; color: Theme.muted }
                            SettingNumber { setting: "convert.resolutionHeight"; from: 1; to: 3000; name: qsTr("Height") }
                        }
                        SettingsRow {
                            text: qsTr("Tags at the start of each line")
                            SettingText { setting: "convert.assTagsToInsertInLine"; Accessible.name: qsTr("Tags at the start of each line") }
                        }
                        SettingCheck { setting: "convert.showSettings"; text: qsTr("Show the conversion window first") }
                    }
                }

                // Subtitle properties (legacy SubtitlesProperties page): the
                // labels pair with the ASS_PROPERTIES_* options as legacy pairs them.
                SettingsPage {
                    objectName: "settingsPageSubtitleProperties"
                    SettingsSection {
                        title: qsTr("Subtitle information")
                        ruled: false
                        GridLayout {
                            columns: 3
                            columnSpacing: 10
                            rowSpacing: 8
                            Layout.leftMargin: 24
                            // The check column's header: a checked field is
                            // written into the subtitles' information
                            // (SubsGrid::SwapAssProperties). (docsLabel: the
                            // docs export's, in English as it runs.)
                            Item { implicitWidth: 1; implicitHeight: 1 }
                            Item { implicitWidth: 1; implicitHeight: 1 }
                            Label {
                                text: qsTr("Use")
                                color: Theme.muted
                                HoverHandler { id: useHover }
                                ToolTip.visible: useHover.hovered
                                ToolTip.text: qsTr("Write the value into the subtitles' information")
                            }
                            Label { text: qsTr("Title"); Layout.preferredWidth: 120 }
                            SettingText { setting: "scriptProperties.title"; Layout.preferredWidth: 300; Accessible.name: qsTr("Title") }
                            SettingCheck { setting: "scriptProperties.titleOn"; Accessible.name: qsTr("Title"); readonly property string docsLabel: "Write Title into the subtitles" }
                            Label { text: qsTr("Author") }
                            SettingText { setting: "scriptProperties.script"; Layout.preferredWidth: 300; Accessible.name: qsTr("Author") }
                            SettingCheck { setting: "scriptProperties.scriptOn"; Accessible.name: qsTr("Author"); readonly property string docsLabel: "Write Author into the subtitles" }
                            Label { text: qsTr("Translator") }
                            SettingText { setting: "scriptProperties.translation"; Layout.preferredWidth: 300; Accessible.name: qsTr("Translator") }
                            SettingCheck { setting: "scriptProperties.translationOn"; Accessible.name: qsTr("Translator"); readonly property string docsLabel: "Write Translator into the subtitles" }
                            Label { text: qsTr("Proofreading") }
                            SettingText { setting: "scriptProperties.editing"; Layout.preferredWidth: 300; Accessible.name: qsTr("Proofreading") }
                            SettingCheck { setting: "scriptProperties.editingOn"; Accessible.name: qsTr("Proofreading"); readonly property string docsLabel: "Write Proofreading into the subtitles" }
                            Label { text: qsTr("Timer") }
                            SettingText { setting: "scriptProperties.timing"; Layout.preferredWidth: 300; Accessible.name: qsTr("Timer") }
                            SettingCheck { setting: "scriptProperties.timingOn"; Accessible.name: qsTr("Timer"); readonly property string docsLabel: "Write Timer into the subtitles" }
                            Label { text: qsTr("Editing") }
                            SettingText { setting: "scriptProperties.update"; Layout.preferredWidth: 300; Accessible.name: qsTr("Editing") }
                            SettingCheck { setting: "scriptProperties.updateOn"; Accessible.name: qsTr("Editing"); readonly property string docsLabel: "Write Editing into the subtitles" }
                        }
                    }
                    SettingsSection {
                        title: qsTr("Files")
                        SettingCheck { setting: "scriptProperties.askForChange"; text: qsTr("Always ask before changing subtitle information") }
                        // WS2: the tab's panel layout in the ASS file's [Hikari] section.
                        SettingCheck { setting: "subtitles.layoutInFile"; text: qsTr("Save the panel layout in subtitle files") }
                    }
                }

                // Automation: the automation settings from legacy Editor and Editor > Advanced.
                SettingsPage {
                    objectName: "settingsPageAutomation"
                    SettingsSection {
                        title: qsTr("Loading")
                        ruled: false
                        SettingsRow {
                            iconRole: "automation"
                            text: qsTr("Load scripts")
                            SettingChoice {
                                setting: "automation.loadingMethod"
                                model: [qsTr("After program start asynchronously"), qsTr("After program start"),
                                        qsTr("After open menu asynchronously"), qsTr("After open menu")]
                                Accessible.name: qsTr("Load scripts")
                            }
                        }
                        SettingCheck { setting: "automation.oldScriptsCompatibility"; text: qsTr("Compatibility with older HikariSub scripts") }
                    }
                    SettingsSection {
                        title: qsTr("Debugging")
                        SettingsRow {
                            text: qsTr("Lua trace level")
                            SettingNumber { setting: "automation.traceLevel"; from: 0; to: 5; name: qsTr("Lua trace level") }
                        }
                    }
                    // AD2 (#311): script dialogs' last answers (AD2-fill-last-answers).
                    SettingsSection {
                        objectName: "settingsScriptDialogsGroup"
                        title: qsTr("Script dialogs")
                        SettingCheck { setting: "automation.fillLastAnswers"; text: qsTr("Fill in my last answers") }
                        SettingNote { text: qsTr("Script dialogs open with the answers you gave last time. A value the script works out itself, from the selection say, still wins. Off: they open with the script’s values, and ⋯ › Last used fills yours in.") }
                    }
                    SettingsSection {
                        title: qsTr("Kept answers and presets")
                        SettingsRow {
                            text: qsTr("Last answers")
                            note: qsTr("Presets are kept; delete them from a dialog’s ⋯ menu.")
                            Button {
                                objectName: "settingsClearAnswers"
                                text: qsTr("Clear last answers…")
                                enabled: (dialog.values["automation.dialogAnswers"] || []).length > 0
                                onClicked: dialog.clearLastAnswers()
                            }
                        }
                    }
                }

                // O2: Hotkeys (legacy Hotkeyss page): the context filter and
                // PF1's search, the Function / Hotkey list under its
                // contexts' headings (a double click maps; Ctrl+Z / Ctrl+Y
                // undo and redo in it), Map hotkey, Restore default and
                // Delete. Changed rows show in the warning colour until
                // OK/Apply.
                Item {
                    objectName: "settingsPageHotkeys"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 24
                        anchors.rightMargin: 24
                        anchors.topMargin: 20
                        anchors.bottomMargin: 20
                        spacing: 10
                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            ComboBox {
                                id: hotkeyFilter
                                objectName: "hotkeyFilter"
                                Layout.preferredWidth: 200
                                model: [qsTr("All"), qsTr("Set shortcuts"), qsTr("Global shortcuts"), qsTr("Subtitle shortcuts"),
                                        qsTr("Editor shortcuts"), qsTr("Video shortcuts"), qsTr("Audio Shortcuts"),
                                        qsTr("Automation shortcuts")]
                                ToolTip.text: qsTr("Filtering mode:")
                                ToolTip.visible: hovered
                                Accessible.name: qsTr("Choose filtering")
                                onActivated: index => dialog.hotkeys.filter(index)
                            }
                            TextField {
                                id: hotkeySearch
                                objectName: "hotkeySearch"
                                placeholderText: qsTr("Search hotkeys")
                                leftPadding: 30
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Search hotkeys")
                                onTextChanged: if (dialog.hotkeys) dialog.hotkeys.search(text)
                                Icon {
                                    iconRole: "search"
                                    color: Theme.muted
                                    x: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            // PU7 (#309): AM1's import, moved here from the
                            // Automation panel's menu (PU7-import-hotkeys).
                            IconTextButton {
                                objectName: "hotkeyImportLegacy"
                                iconRole: "import"
                                text: qsTr("Import legacy hotkeys…")
                                enabled: dialog.hotkeys !== null && !dialog.hotkeys.optionsModified
                                tip: enabled ? qsTr("The script hotkeys of a legacy Hotkeys.txt, added at once")
                                             : qsTr("Press Apply or Cancel first: the import is added at once")
                                onClicked: legacyHotkeysFile.open()
                            }
                        }
                        ListView {
                            id: hotkeyList
                            objectName: "hotkeyList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            focus: true
                            model: dialog.hotkeys ? dialog.hotkeys.rows : []
                            // The list's selection (a shown position, -1 for none).
                            readonly property int selectedRow: dialog.hotkeys ? dialog.hotkeys.selected : -1
                            ScrollBar.vertical: ScrollBar {}
                            Accessible.name: qsTr("Hotkeys")
                            // The heading a row is listed under: its window, or Automation for a macro.
                            readonly property var contexts: [qsTr("Global"), qsTr("Subtitle grid"), qsTr("Line editor"),
                                                             qsTr("Video"), qsTr("Audio"), qsTr("Automation")]
                            function contextOf(row) { return row.script !== undefined ? 5 : row.context }
                            // HikariListCtrl's gutter while the list is filtered: a
                            // box where rows are hidden ("+") or shown again ("-");
                            // a click shows or hides them (ShowOrHideBlock).
                            // PF1: not while searching (the search hides rows on purpose).
                            readonly property bool gutter: dialog.hotkeys ? dialog.hotkeys.listFiltered && hotkeySearch.text.length === 0
                                                                          : false
                            component BlockBox: Label {
                                property int block: 0
                                property int position: -1
                                visible: block !== 0 && hotkeyList.gutter
                                text: block === 1 ? "+" : "−"
                                horizontalAlignment: Text.AlignHCenter
                                width: 13
                                Accessible.role: Accessible.Button
                                Accessible.name: block === 1 ? qsTr("Show hidden rows") : qsTr("Hide the rows shown")
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: dialog.hotkeys.toggleBlock(parent.position)
                                }
                            }
                            Rectangle {
                                parent: hotkeyList
                                anchors.fill: parent
                                z: -1
                                color: Theme.field
                                border.color: Theme.line
                            }
                            header: Rectangle {
                                width: hotkeyList.width
                                height: 26
                                color: Theme.raised
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    BlockBox {
                                        objectName: "hotkeyTopBlock"
                                        block: dialog.hotkeys ? dialog.hotkeys.topBlock : 0
                                        Layout.preferredWidth: hotkeyList.gutter ? 13 : 0
                                    }
                                    Label { text: qsTr("Function"); color: Theme.muted; Layout.preferredWidth: hotkeyList.width * 0.65 }
                                    Label { text: qsTr("Hotkey"); color: Theme.muted; Layout.fillWidth: true }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                            }
                            delegate: Column {
                                id: hotkeyEntry
                                required property var modelData
                                required property int index
                                width: hotkeyList.width
                                // A heading where the context changes.
                                readonly property bool headed: index === 0
                                    || hotkeyList.contextOf(hotkeyList.model[index - 1]) !== hotkeyList.contextOf(modelData)
                                Label {
                                    objectName: "hotkeyHeading"
                                    visible: hotkeyEntry.headed
                                    text: hotkeyList.contexts[hotkeyList.contextOf(hotkeyEntry.modelData)] ?? ""
                                    font.bold: true
                                    color: Theme.muted
                                    leftPadding: 10
                                    topPadding: 6
                                    bottomPadding: 2
                                    Accessible.role: Accessible.Heading
                                }
                                ItemDelegate {
                                    id: hotkeyRow
                                    readonly property var modelData: hotkeyEntry.modelData
                                    readonly property int index: hotkeyEntry.index
                                    width: hotkeyList.width
                                    highlighted: index === hotkeyList.selectedRow
                                    leftPadding: 22
                                    onClicked: {
                                        hotkeyList.forceActiveFocus()
                                        dialog.hotkeys.select(index)
                                    }
                                    onDoubleClicked: {
                                        dialog.hotkeys.select(index)
                                        dialog.mapHotkey(hotkeyRow)
                                    }
                                    // AM1: a macro's row names its script and macro, and
                                    // says in the warning colour why its keys do not run it.
                                    readonly property bool macro: modelData.script !== undefined
                                    Accessible.name: (macro ? modelData.script + " " + modelData.macro : modelData.text) + " " + modelData.accel
                                                     + (macro && modelData.note.length > 0 ? ", " + modelData.note : "")
                                    background: Rectangle {
                                        color: hotkeyRow.highlighted ? Theme.select : "transparent"
                                        Rectangle { visible: hotkeyRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                                    }
                                    contentItem: RowLayout {
                                        Item {
                                            // the block's line beside rows shown from it
                                            visible: hotkeyList.gutter
                                            Layout.preferredWidth: 13
                                            Layout.fillHeight: true
                                            Rectangle {
                                                visible: hotkeyRow.modelData.inBlock
                                                x: 6; width: 1; height: parent.height
                                                color: Theme.text
                                            }
                                        }
                                        // ItemText / ItemHotkey (HikariListCtrl.cpp:44-55,
                                        // OptionsDialog.cpp:48-71): a cell whose text does
                                        // not fit shows it whole as its tooltip (the
                                        // Function column's first 1000 characters). Under
                                        // its heading a row shows its name (PF1).
                                        Label {
                                            id: hotkeyText
                                            objectName: "hotkeyText"
                                            text: hotkeyRow.macro
                                                  ? (modelData.macro.length > 0 ? modelData.script + " › " + modelData.macro
                                                                                : modelData.script + " › " + qsTr("Missing macro"))
                                                  : modelData.name
                                            color: modelData.textModified && !hotkeyRow.macro ? dialog.warningColour : Theme.text
                                            elide: Text.ElideRight
                                            Layout.preferredWidth: hotkeyList.width * 0.65 - hotkeyRow.leftPadding + 10
                                            HoverHandler { id: textHover }
                                            ToolTip.visible: textHover.hovered && truncated
                                            ToolTip.text: (hotkeyRow.macro ? hotkeyText.text : modelData.text).substring(0, 1000)
                                        }
                                        Label {
                                            id: hotkeyKeys
                                            objectName: "hotkeyKeys"
                                            text: hotkeyRow.macro && modelData.note.length > 0
                                                  ? (modelData.accel.length > 0 ? modelData.accel + " · " + modelData.note : modelData.note)
                                                  : modelData.accel
                                            color: modelData.keyModified || (hotkeyRow.macro && modelData.note.length > 0) ? dialog.warningColour
                                                 : Theme.muted
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                            HoverHandler { id: keysHover }
                                            ToolTip.visible: keysHover.hovered && truncated
                                            ToolTip.text: hotkeyKeys.text
                                        }
                                    }
                                    // the box for the gap after this row
                                    BlockBox {
                                        objectName: "hotkeyBlock" + hotkeyRow.index
                                        block: hotkeyRow.modelData.block
                                        position: hotkeyRow.index
                                        y: hotkeyRow.height - height / 2
                                        z: 1
                                    }
                                }
                            }
                            // HikariListCtrl's accelerators: undo, redo, and the selection.
                            function ownKey(event) {
                                const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
                                return (mods === Qt.ControlModifier && (event.key === Qt.Key_Z || event.key === Qt.Key_Y))
                                    || (mods === 0 && (event.key === Qt.Key_Up || event.key === Qt.Key_Down))
                            }
                            Keys.onShortcutOverride: event => event.accepted = ownKey(event)
                            Keys.onPressed: event => {
                                const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
                                if (mods === Qt.ControlModifier && event.key === Qt.Key_Z)
                                    dialog.hotkeys.optionsUndo()
                                else if (mods === Qt.ControlModifier && event.key === Qt.Key_Y)
                                    dialog.hotkeys.optionsRedo()
                                else if (mods === 0 && event.key === Qt.Key_Up)
                                    dialog.hotkeys.select(dialog.hotkeys.selected - 1)
                                else if (mods === 0 && event.key === Qt.Key_Down)
                                    dialog.hotkeys.select(dialog.hotkeys.selected + 1)
                                else
                                    return
                                event.accepted = true
                            }
                        }
                        RowLayout {
                            spacing: 8
                            Button {
                                id: hotkeyMapButton
                                objectName: "hotkeyMap"
                                text: qsTr("Map hotkey…")
                                // The row's note says why (AM1: a name two loaded scripts share).
                                enabled: dialog.hotkeys.mapProblem.length === 0
                                onClicked: dialog.mapHotkey(hotkeyMapButton)
                            }
                            Button {
                                objectName: "hotkeyRestore"
                                text: qsTr("Restore default")
                                onClicked: dialog.hotkeys.optionsReset()
                            }
                            Button { objectName: "hotkeyRemove"; text: qsTr("Delete"); onClicked: dialog.hotkeys.optionsDelete() }
                        }
                    }
                }

                // PU7 (#309): Updates, the three options that left the update
                // window (PU7-update-settings) and the version "Skip this
                // version" hid, staged as every other setting. An install
                // that does not install updates itself (AUR, portable) shows
                // why in place of "Install updates automatically"; an AUR
                // install with a channel has no "Stable versions only".
                SettingsPage {
                    objectName: "settingsPageUpdates"
                    SettingsSection {
                        title: qsTr("Checking")
                        ruled: false
                        SettingCheck { setting: "updater.autoCheck"; text: qsTr("Check for updates automatically") }
                        SettingNote { text: qsTr("At most once a day, when HikariSub starts.") }
                        SettingCheck {
                            setting: "updater.checkForStable"
                            visible: !dialog.updates || dialog.updates.stableOnlyOption
                            text: qsTr("Stable versions only")
                        }
                        SettingNote {
                            visible: !dialog.updates || dialog.updates.stableOnlyOption
                            text: qsTr("Off: prereleases (beta versions) are offered too.")
                        }
                        SettingsRow {
                            // DX1 (#367): the docs export's entry; shown only
                            // once a version is skipped, Offer it again clears it.
                            readonly property string docsSetting: "updater.skippedVersion"
                            readonly property string docsControl: "readonly"
                            readonly property string docsLabel: text
                            visible: (dialog.values["updater.skippedVersion"] ?? "").length > 0
                            text: qsTr("Skipped version")
                            Label {
                                objectName: "settingsSkippedVersion"
                                text: dialog.values["updater.skippedVersion"] ?? ""
                            }
                            Button {
                                objectName: "settingsOfferAgain"
                                text: qsTr("Offer it again")
                                onClicked: dialog.put("updater.skippedVersion", "")
                            }
                        }
                        SettingNote {
                            visible: (dialog.values["updater.skippedVersion"] ?? "").length > 0
                            text: qsTr("Skip this version hides one version; a newer one is offered again.")
                        }
                    }
                    SettingsSection {
                        title: qsTr("Installing")
                        SettingCheck {
                            setting: "updater.autoInstall"
                            visible: !dialog.updates || dialog.updates.installsUpdates
                            text: qsTr("Install updates automatically")
                        }
                        SettingNote {
                            objectName: "settingsInstallNote"
                            text: !dialog.updates || dialog.updates.installsUpdates
                                  ? qsTr("Downloads the update in the background and installs it when you quit HikariSub.")
                                  : dialog.updates.aur
                                    ? qsTr("You installed HikariSub from the AUR, so your package manager installs updates.")
                                    : qsTr("This copy does not install updates itself: the update window opens the download page.")
                            Layout.leftMargin: !dialog.updates || dialog.updates.installsUpdates ? 24 : 0
                            Layout.topMargin: !dialog.updates || dialog.updates.installsUpdates ? -5 : 0
                        }
                    }
                }
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
    }

    FolderDialog {
        id: fontsFolderDialog
        objectName: "fontsFolderDialog"
        title: qsTr("Choose external font folder") // TI1-fonts-folder-title
        onAccepted: {
            const path = dialog.app.settingsFolderPath(selectedFolder)
            fontsFolder.text = path
            dialog.put("fonts.externalDirectory", path)
        }
    }

    // PU7 (#309): Import legacy hotkeys: what was added at once, as a toast
    // (NT1's rule for an outcome); the skipped ones under Details.
    FileDialog {
        id: legacyHotkeysFile
        objectName: "legacyHotkeysFile"
        title: qsTr("Choose a legacy Hotkeys.txt")
        nameFilters: [qsTr("Hotkeys (Hotkeys.txt)"), qsTr("All files (*)")]
        onAccepted: dialog.importLegacyHotkeys(dialog.app.localPath(selectedFile))
    }
    function importLegacyHotkeys(path) {
        const r = hotkeys.importLegacyScripts(path)
        hotkeys.search(hotkeySearch.text)
        if (!notices)
            return r
        const title = qsTr("Import legacy hotkeys")
        if (r.read !== true) {
            notices.post("error", title, qsTr("The file could not be read."), { details: path })
            return r
        }
        const parts = [r.added > 0 ? qsTr("Added %n script hotkey(s).", "", r.added) : qsTr("No script hotkeys were added.")]
        if (r.kept > 0)
            parts.push(qsTr("%n kept the keys you gave them.", "", r.kept))
        if (r.skipped > 0)
            parts.push(qsTr("%n skipped: their keys are taken.", "", r.skipped))
        notices.post("info", title, parts.join(" "), r.skipped > 0 ? { details: r.skippedNames.join("\n") } : {})
        return r
    }

    QtObject {
        id: themeColourTarget
        property string setting: ""
        property string before: ""
        function changeColour(c) {
            dialog.put(setting, dialog.colourPicker.htmlText(c))
            return true
        }
        function endDialog(accepted) {
            if (!accepted)
                dialog.put(setting, before)
        }
    }
    ColourPopover {
        id: themeColourPopover
        objectName: "themeColourPopover"
        picker: dialog.colourPicker
    }

    HotkeyCapture {
        id: settingsHotkeyMapping
        objectName: "settingsHotkeyMapping"
        hotkeys: dialog.hotkeys
        settings: dialog.app ? dialog.app.settings : null
    }

    // PK1's message box: the "Reset all settings…" question.
    Loader {
        id: questionLoader
        active: false
        sourceComponent: Component {
            MessageBox {
                objectName: "settingsQuestion"
                settings: dialog.settings
            }
        }
    }
}
