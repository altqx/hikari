pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "settingsDialog"
    required property var app
    required property ColourPickerController colourPicker
    property var hotkeys: null
    property var updates: null
    property var crashReports: null
    property var ai: null
    property var inlineAi: null
    property var skills: null
    property var pluginManager: null
    property TagButtonsController tagButtons: null
    property var importer: null
    property string legacyNotFound: ""
    title: qsTr("Settings")
    dialogId: "settings"
    settings: app ? app.settings : null
    modal: true
    resizable: true
    flush: true
    defaultButton: closeButton
    initialFocus: searchField
    property var values: ({})
    property var languages: []
    property var dictionaries: []
    property var catalogs: []
    property var styles: []
    readonly property var newFileCatalogs: [{value: "", text: qsTr("Built-in Default")}].concat(catalogs.map(name => ({value: name, text: name})))
    property var renderers: []
    property var rendererLabels: []
    property var outputDevices: []
    property ToastStack notices: null
    property var tagButtonList: []
    property int shellWindow: 0
    property bool hasProject: false
    property bool projectTab: false
    property var projectWaiting: []
    property var overrides: []
    property var changedIds: []
    property var projectValues: ({})
    property var projectYours: ({})
    property var projectSources: ({})
    property string query: ""
    property string landingNote: ""
    property string flashSetting: ""
    property var rowItems: ({})
    property var sectionItems: ({})
    property var folded: ({})
    property string currentNode: ""
    readonly property string currentPage: currentNode.split(".")[0]
    readonly property bool searching: query.trim().length > 0
    readonly property var pluginRows: {
        const out = []
        for (const plugin of pluginManager ? pluginManager.plugins : [])
            for (const s of plugin.settings ?? [])
                out.push({ setting: "plugin." + plugin.id + "." + s.id, plugin: plugin.id, pluginName: plugin.name,
                           label: s.title ?? s.id, description: s.description ?? "", keywords: s.keywords ?? [],
                           projectable: s.scope !== "user" && s.type !== "secret" })
        return out
    }
    property var projectPlugins: []
    readonly property var pluginsWithSettings: (pluginManager ? pluginManager.plugins : []).filter(p => (p.settings ?? []).length > 0)
    readonly property var found: SettingsCatalogue.search(query, pluginRows, projectTab ? Object.keys(projectSources).filter(id => projectSources[id] === "project") : changedIds, projectTab)
    readonly property var catalogue: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.pages() : [])
    signal reloaded()
    signal creditsRequested()
    signal legacyImportRequested()
    signal legacyRollBackRequested()
    readonly property VideoFollowChoices follow: VideoFollowChoices { settings: dialog.app.settings }

    function openDialog(page, filter, projectPage) {
        const r = app.openSettingsDialog()
        languages = r.languages
        dictionaries = r.dictionaries
        catalogs = r.catalogs
        styles = r.styles
        renderers = r.renderers
        rendererLabels = r.rendererLabels
        outputDevices = r.outputDevices
        values = r.values
        hasProject = app.projectRoot(shellWindow).length > 0
        projectTab = hasProject && projectPage !== undefined
        refreshChanged()
        if (hasProject)
            loadProject()
        copyTagButtons()
        legacyNotFound = ""
        if (importer && importer.available) {
            importer.discover()
            if (importer.roots.length === 0)
                legacyNotFound = importer.status
        }
        reloaded()
        opened()
        if (filter !== undefined) {
            hotkeyFilter.currentIndex = Math.max(0, hotkeyFilter.modes.indexOf(filter))
            hotkeys.filter(filter)
        }
        const firstOpen = !visible
        open()
        if (firstOpen || page !== undefined || projectPage !== undefined) {
            query = ""
            landingNote = ""
            Qt.callLater(() => dialog.showNode(projectPage ?? page ?? (settings ? settings.value("workspace.settingsPage") : "general")))
        }
        if (notices)
            for (const warning of r.warnings)
                notices.post("warning", qsTr("Settings"), warning)
    }
    // An in-app link: a page or section id opens there, anything else lands on its row.
    function openAt(id) {
        if (isNode(id))
            openDialog(id)
        else
            openSetting(id, false)
    }
    // A setting link: opens the window at the row, never changing a value.
    function openSetting(setting, project) {
        openDialog()
        searchField.text = ""
        landingNote = ""
        const projectOk = project && hasProject && (setting.length === 0 || isProjectable(setting))
        if (project && !projectOk)
            landingNote = hasProject ? qsTr("A project can’t set “%1”, so it opens on the Yours tab.").arg(setting)
                                     : qsTr("This window has no project, so the link opens the Yours tab.")
        projectTab = projectOk
        if (setting.length === 0)
            return
        if (!rowItem(setting) && !SettingsCatalogue.sectionOf(setting).length && !pluginRows.some(r => r.setting === setting)) {
            searchField.text = setting
            return
        }
        Qt.callLater(() => dialog.land(setting))
    }
    function isNode(id) {
        return catalogue.some(p => p.id === id) || sectionInfo(id) !== undefined
    }
    function land(setting) {
        const section = SettingsCatalogue.sectionOf(setting) || pluginSection(setting)
        unfold((section || setting).split(".")[0])
        const item = rowItem(setting)
        if (!item)
            return
        scrollTo(item, rowsView.height / 3)
        rowFlash.row = item
        flashSetting = setting
        flashEnd.restart()
        const control = firstControl(item)
        if (control)
            control.forceActiveFocus(Qt.OtherFocusReason)
        item.Accessible.announce(item.text)
    }
    function pluginSection(setting) {
        const r = pluginRows.find(r => r.setting === setting)
        return r ? "plugins." + r.plugin : ""
    }
    function firstControl(item) {
        for (const child of item.children ?? []) {
            if (child.activeFocusOnTab && child.visible && child.enabled && child.objectName.startsWith("setting_"))
                return child
            const deeper = firstControl(child)
            if (deeper)
                return deeper
        }
        return null
    }
    function isProjectable(setting) {
        if (setting.startsWith("plugin."))
            return pluginRows.some(r => r.setting === setting && r.projectable)
        return SettingsCatalogue.projectable(setting)
    }
    function tabKey(key, project) { return (project ? "project:" : "") + key }
    function registerRow(setting, item, project) {
        rowItems[tabKey(setting, project)] = item
    }
    function registerSection(key, item, project) {
        sectionItems[tabKey(key, project)] = item
    }
    function rowItem(setting) { return rowItems[tabKey(setting, projectTab)] }
    function shows(setting) {
        if (projectTab && !isProjectable(setting))
            return false
        return !searching || found.settings[setting] === true
    }
    function sectionShown(key) {
        const section = sectionInfo(key)
        if (section && !(projectTab ? section.project : section.yours))
            return false
        return !searching || (found.sections[key] ?? 0) > 0
    }
    function pageShown(page) {
        if (!searching)
            return projectTab ? pageHasProjectSections(page) : true
        return (found.pages[page] ?? 0) > 0
    }
    function pageHasProjectSections(page) {
        if (page === "plugins")
            return pluginsWithSettings.some(p => pluginRows.some(r => r.plugin === p.id && r.projectable))
        const p = catalogue.find(p => p.id === page)
        return p !== undefined && p.sections.some(s => s.project)
    }
    function sectionInfo(key) {
        const page = catalogue.find(p => p.id === key.split(".")[0])
        return page ? page.sections.find(s => s.id === key) : undefined
    }

    function refreshChanged() {
        changedIds = settings ? settings.changedSettings() : []
        overrides = hasProject ? app.projectOverrides(shellWindow) : []
    }
    function loadProject() {
        const r = app.openProjectSettings(shellWindow)
        projectValues = r.values ?? {}
        projectYours = r.yours ?? {}
        projectSources = r.sources ?? {}
        projectWaiting = app.projectWaitsForTrust(shellWindow)
        overrides = app.projectOverrides(shellWindow)
        projectPlugins = app.projectPluginSettings(shellWindow)
        projectPages.load()
    }
    function isChanged(ids) {
        if (projectTab)
            return ids.some(id => projectSources[id] === "project" || (id.startsWith("plugin.") && projectSetsPlugin(id)))
        return ids.some(id => changedIds.includes(id) || (id.startsWith("plugin.") && pluginChanged(id)))
    }
    function pluginChanged(id) {
        const r = pluginSetting(id)
        return r !== undefined && r.value !== undefined && JSON.stringify(r.value) !== JSON.stringify(r.default)
    }
    function pluginSetting(id) {
        const found = pluginEntry(id)
        return found ? found.setting : undefined
    }
    function pluginEntry(id, plugins) {
        for (const plugin of plugins ?? (pluginManager ? pluginManager.plugins : []))
            for (const s of plugin.settings ?? [])
                if ("plugin." + plugin.id + "." + s.id === id)
                    return { plugin: plugin.id, setting: s }
        return undefined
    }
    function projectSetsPlugin(id) {
        const found = pluginEntry(id, projectPlugins)
        return found !== undefined && found.setting.project !== undefined
    }
    function keepPluginSetting(plugin, setting, value) {
        if (!projectTab)
            return pluginManager.setSetting(plugin, setting, JSON.stringify(value ?? null))
        const problem = app.setProjectPluginSetting(shellWindow, plugin, setting, JSON.stringify(value ?? null))
        loadProject()
        return problem
    }
    // {text, link?, readOnly?, warning?} or null: where else the row's setting is set.
    function elsewhere(setting) {
        if (!hasProject)
            return null
        if (setting.startsWith("plugin.")) {
            const set = projectSetsPlugin(setting)
            if (!projectTab)
                return set ? { text: qsTranslate("HikariSub.Project", "Set in this project"), link: "project" } : null
            return set ? { text: qsTranslate("HikariSub.Project", "Set in this project") }
                       : { text: qsTranslate("HikariSub.Project", "Uses yours"), link: "yours" }
        }
        if (!projectTab)
            return overrides.includes(setting) ? { text: qsTranslate("HikariSub.Project", "Set in this project"), link: "project" } : null
        const source = projectSources[setting]
        if (source === "local")
            return { text: qsTranslate("HikariSub.Project", "Set in project.local.toml"), readOnly: true }
        if (source === "project" && projectWaiting.includes(setting))
            return { text: qsTr("Waits for trust"), warning: true, link: "trust" }
        if (source === "project")
            return { text: qsTranslate("HikariSub.Project", "Set in this project") }
        return { text: qsTranslate("HikariSub.Project", "Uses yours"), link: "yours" }
    }
    function follow(link, setting) {
        if (link === "trust") {
            projectTab = false
            Qt.callLater(() => dialog.showNode("general.trustedFolders"))
            return
        }
        projectTab = link === "project"
        Qt.callLater(() => dialog.land(setting))
    }

    // Changes apply at once: a staged value is written as soon as a control changes.
    function put(setting, value) {
        const v = Object.assign({}, values)
        v[setting] = value
        values = v
        app.applySettings(values, false)
        if (setting.startsWith("appearance."))
            Theme.endPreview()
        refreshChanged()
    }
    function projectPut(setting, value) {
        const v = Object.assign({}, projectValues)
        v[setting] = value
        projectValues = v
        const changed = {}
        changed[setting] = value
        app.applyProjectSettings(shellWindow, changed, [])
        loadProject()
    }
    function resetSetting(ids) {
        if (projectTab) {
            for (const id of ids.filter(id => id.startsWith("plugin."))) {
                const found = pluginEntry(id, projectPlugins)
                if (found)
                    app.setProjectPluginSetting(shellWindow, found.plugin, found.setting.id, "null")
            }
            app.applyProjectSettings(shellWindow, {}, ids.filter(id => !id.startsWith("plugin.")))
            loadProject()
            reloaded()
            return
        }
        for (const id of ids) {
            if (id.startsWith("plugin.")) {
                const found = pluginEntry(id)
                if (found)
                    pluginManager.setSetting(found.plugin, found.setting.id, JSON.stringify(found.setting.default ?? null))
                continue
            }
            settings.reset(id)
        }
        values = app.currentSettings()
        refreshChanged()
        reloaded()
    }
    function copyText(text) { app.copyText(text) }
    function linkTo(setting) { return "hikarisub://settings/" + setting + (projectTab ? "?scope=project" : "") }
    function openSettingsFile(setting) {
        if (projectTab)
            app.openSettingsFile(shellWindow, setting && projectSources[setting] === "local" ? "local" : "project")
        else if ((setting ?? "").startsWith("plugin.") || currentNode.startsWith("plugins"))
            app.openSettingsFile(shellWindow, "plugins")
        else if (currentNode === "hotkeys")
            app.openSettingsFile(shellWindow, "hotkeys")
        else
            app.openSettingsFile(shellWindow, "settings")
    }
    Connections {
        target: dialog.settings
        function onChanged(id) {
            if (!dialog.visible || reloadValues.running)
                return
            reloadValues.restart()
        }
    }
    Timer {
        id: reloadValues
        interval: 0
        onTriggered: {
            const current = dialog.app.currentSettings()
            dialog.refreshChanged()
            if (JSON.stringify(current) === JSON.stringify(dialog.values))
                return
            dialog.values = current
            dialog.reloaded()
        }
    }
    Timer {
        id: flashEnd
        interval: 50
        onTriggered: dialog.flashSetting = ""
    }
    Connections {
        target: dialog.hotkeys
        function onRowsChanged() {
            if (dialog.visible && dialog.hotkeys.optionsModified)
                dialog.hotkeys.commitOptions()
        }
    }

    Connections {
        target: dialog.app
        function onSettingsListsChanged(dictionaries) { dialog.dictionaries = dictionaries }
    }
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
    readonly property var question: questionLoader.item
    function ask(options, done) {
        questionLoader.active = true
        questionLoader.item.ask(options, done)
    }
    function pickThemeColour(theme, role, colour, anchor, name) {
        themeColourPopover.openFor(anchor, themeColourTarget, () => {
            const now = colour
            themeColourTarget.theme = theme
            themeColourTarget.role = role
            themeColourTarget.before = values["appearance.colours"] ?? []
            colourPicker.opened("settings")
            const c = colourPicker.htmlColour(now)
            return { r: c.r, g: c.g, b: c.b, a: 0 }
        }, { title: name, alpha: false, focus: anchor })
    }
    function resetPage(page) {
        if (page === "hotkeys") {
            hotkeys.optionsResetAll()
            return
        }
        values = app.resetSettingsPage(values, page)
        app.applySettings(values, false)
        if (page === "lineEditor") {
            tagButtonList = []
            commitTagButtons()
        }
        if (page === "conversion")
            styles = app.settingsStyles()
        refreshChanged()
        reloaded()
    }
    function askResetPage(page) {
        const name = catalogue.find(p => p.id === page)?.name ?? ""
        ask({
            title: qsTr("Reset page"),
            text: qsTr("Reset the settings on %1 to their defaults?").arg(name),
            severity: "warning",
            choices: [{ id: "reset", text: qsTr("Reset page"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, answer => { if (answer === "reset") dialog.resetPage(page) })
    }
    function resetAll() {
        ask({
            title: qsTr("Reset all settings"),
            text: qsTr("Reset all settings to their defaults?"),
            detail: qsTr("Shortcuts and the appearance stay as they are: reset them on their own pages."),
            severity: "warning",
            choices: [
                { id: "reset", text: qsTr("Reset all"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, answer => {
            if (answer !== "reset")
                return
            dialog.values = dialog.app.resetSettings(dialog.values)
            dialog.app.applySettings(dialog.values, true)
            dialog.values = dialog.app.currentSettings()
            dialog.copyTagButtons()
            dialog.refreshChanged()
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
        commitTagButtons()
    }
    function removeTagButton(index) {
        const list = tagButtonList.slice()
        list.splice(index, 1)
        tagButtonList = list
        tagButtonRows.currentIndex = Math.min(index, list.length - 1)
        commitTagButtons()
    }
    function moveTagButton(index, step) {
        const list = tagButtonList.slice()
        const b = list[index]
        list[index] = list[index + step]
        list[index + step] = b
        tagButtonList = list
        tagButtonRows.currentIndex = index + step
        commitTagButtons()
    }
    readonly property color warningColour: Theme.warning
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
        }, { anchor: row && row.visible ? row : from, clear: () => hotkeys.optionsDelete(), command: target.id })
    }
    function chooseCatalog(index) {
        const r = app.chooseSettingsCatalog(values, index)
        styles = r.styles
        put("convert.styleCatalog", values["convert.styleCatalog"])
        values = Object.assign({}, values, r.values)
        app.applySettings(values, false)
        reloaded()
    }

    onVisibleChanged: if (!visible) {
        Theme.endPreview()
        aiPage.closeForm()
    }

    // The tree: pages, their sections, then Plugins with one node per plugin.
    readonly property var treeRows: {
        const out = []
        const add = (page, name, icon, sections) => {
            if (!pageShown(page))
                return
            const shownSections = sections.filter(s => sectionShown(s.id))
            const open = searching || folded[page] === false
            out.push({ id: page, name: name, icon: icon, depth: 0, folded: !open, foldable: shownSections.length > 0,
                       count: searching ? (found.pages[page] ?? 0) : -1 })
            if (open)
                for (const s of shownSections)
                    out.push({ id: s.id, name: s.title, icon: "", depth: 1, folded: false, foldable: false,
                               count: searching ? (found.sections[s.id] ?? 0) : -1 })
        }
        for (const p of catalogue)
            add(p.id, p.name, p.icon, p.sections)
        add("plugins", qsTr("Plugins"), "plugins", pluginsWithSettings.map(p => ({ id: "plugins." + p.id, title: p.name, project: true, yours: true })))
        return out
    }
    function unfold(page) {
        const f = Object.assign({}, folded)
        f[page] = false
        folded = f
    }
    function toggleFold(page) {
        const f = Object.assign({}, folded)
        f[page] = f[page] === false
        folded = f
    }
    function showNode(id) {
        if (!id)
            return
        const page = id.split(".")[0]
        unfold(page)
        const item = id.includes(".") ? sectionItems[tabKey(id, projectTab)] : pageItems[tabKey(page, projectTab)]
        if (item)
            scrollTo(item, 0)
        currentNode = id
        if (settings && !projectTab)
            settings.setValue("workspace.settingsPage", page)
    }
    readonly property var pageItems: ({})
    function scrollTo(item, offset) {
        const y = item.mapToItem(rowsColumn, 0, 0).y
        rowsView.contentItem.contentY = Math.max(0, Math.min(y - offset, rowsColumn.height - rowsView.height))
    }
    function followScroll() {
        const top = rowsView.contentItem.contentY + 8
        let best = ""
        let bestY = -1
        for (const tabbed in sectionItems) {
            if (tabbed.startsWith("project:") !== projectTab)
                continue
            const key = tabbed.replace("project:", "")
            const item = sectionItems[tabbed]
            if (!item || !item.visible)
                continue
            const y = item.mapToItem(rowsColumn, 0, 0).y
            if (y <= top && y > bestY) {
                best = key
                bestY = y
            }
        }
        if (best.length > 0 && best !== currentNode) {
            currentNode = best
            if (settings && !projectTab)
                settings.setValue("workspace.settingsPage", best.split(".")[0])
        }
    }

    leadingActions: [
        IconTextButton {
            objectName: "settingsResetAll"
            iconRole: "restore-default"
            text: qsTr("Reset all…")
            visible: !dialog.projectTab
            tip: qsTr("Resets every setting but the shortcuts and the appearance")
            onClicked: dialog.resetAll()
        }
    ]
    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "settingsClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.close()
        }
    ]

    component SettingsPage: ColumnLayout {
        id: settingsPage
        required property string page
        default property alias content: pageColumn.data
        visible: dialog.pageShown(page)
        spacing: 12
        Layout.fillWidth: true
        Component.onCompleted: dialog.pageItems[dialog.tabKey(page, false)] = settingsPage
        Label {
            objectName: "settingsPageHeading"
            text: dialog.catalogue.find(p => p.id === settingsPage.page)?.name ?? (settingsPage.page === "plugins" ? qsTr("Plugins") : "")
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: Theme.text
            Accessible.role: Accessible.Heading
            Layout.topMargin: 12
        }
        ColumnLayout {
            id: pageColumn
            spacing: 24
            Layout.fillWidth: true
        }
    }
    component Section: SettingsSection {
        host: dialog
    }
    component CreditRow: SettingsRow {
        id: creditRow
        required property var modelData
        readonly property string key: "scriptProperties." + modelData.id
        host: dialog
        setting: key
        settings: [key, key + "On"]
        text: modelData.name
        note: ""
        CheckBox {
            id: creditOn
            objectName: "setting_" + creditRow.key + "On"
            text: qsTr("Write")
            Accessible.name: qsTr("Write %1").arg(creditRow.modelData.name)
            function refresh() { checked = dialog.values[creditRow.key + "On"] === true }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { creditOn.refresh() } }
            onToggled: dialog.put(creditRow.key + "On", checked)
        }
        SettingText {
            setting: creditRow.key
            Accessible.name: creditRow.modelData.name
            Layout.fillWidth: true
        }
    }
    component SettingNote: Label {
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    component SettingCheck: SettingsRow {
        id: checkRow
        property string tip
        property alias checked: check.checked
        host: dialog
        inline: true
        CheckBox {
            id: check
            objectName: "setting_" + checkRow.setting
            text: checkRow.text
            leftPadding: 1
            spacing: 9
            Layout.fillWidth: true
            Accessible.name: text
            Accessible.description: checkRow.note
            ToolTip.text: checkRow.tip
            ToolTip.visible: hovered && checkRow.tip !== ""
            function refresh() { checked = dialog.values[checkRow.setting] === true }
            Component.onCompleted: {
                if (contentItem && contentItem.wrapMode !== undefined)
                    contentItem.wrapMode = Text.Wrap
                refresh()
            }
            Connections { target: dialog; function onReloaded() { check.refresh() } }
            onToggled: dialog.put(checkRow.setting, checked)
        }
    }
    component SettingNumber: RowLayout {
        id: numberRow
        required property string setting
        property string unit
        property string name
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
            Accessible.name: numberRow.name || (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.label(numberRow.setting) : "")
            Accessible.description: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.description(numberRow.setting) : "")
            Layout.preferredWidth: 120
            ToolTip.text: numberRow.tip
            ToolTip.visible: hovered && numberRow.tip !== ""
            function refresh() { value = dialog.values[numberRow.setting] ?? 0 }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { spin.refresh() } }
            onValueModified: typing.restart()
            Timer {
                id: typing
                interval: 500
                onTriggered: dialog.put(numberRow.setting, spin.value)
            }
            onActiveFocusChanged: if (!activeFocus && typing.running) { typing.stop(); typing.triggered() }
            Keys.onReturnPressed: event => { event.accepted = false; if (typing.running) { typing.stop(); typing.triggered() } }
        }
        Label {
            visible: numberRow.unit.length > 0
            text: numberRow.unit
            color: Theme.muted
        }
    }
    component SettingText: TextField {
        id: field
        required property string setting
        property string before: ""
        objectName: "setting_" + setting
        Layout.preferredWidth: 260
        Accessible.description: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.description(setting) : "")
        function refresh() { text = dialog.values[setting] ?? "" }
        function commit() {
            typing.stop()
            if ((dialog.values[setting] ?? "") !== text)
                dialog.put(setting, text)
        }
        Component.onCompleted: refresh()
        Connections { target: dialog; function onReloaded() { if (!field.activeFocus) field.refresh() } }
        onTextEdited: typing.restart()
        onEditingFinished: commit()
        onActiveFocusChanged: if (activeFocus) before = text
        Keys.onEscapePressed: event => {
            if (text === before) {
                event.accepted = false
                return
            }
            typing.stop()
            text = before
            commit()
        }
        Timer {
            id: typing
            interval: 500
            onTriggered: field.commit()
        }
    }
    component SettingChoice: ComboBox {
        id: choice
        required property string setting
        objectName: "setting_" + setting
        Layout.preferredWidth: 260
        Accessible.description: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.description(setting) : "")
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
    component SettingFont: RowLayout {
        id: fontRow
        required property string setting
        required property string sizeSetting
        property string name
        spacing: 8
        FontFamilyBox {
            id: family
            objectName: "setting_" + fontRow.setting
            families: Qt.fontFamilies()
            Accessible.name: fontRow.name
            Layout.preferredWidth: 200
            function refresh() { text = dialog.values[fontRow.setting] ?? "" }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { family.refresh() } }
            onPicked: name => dialog.put(fontRow.setting, name)
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
    // A setting with no control yet: its value, read-only.
    component ValueRow: SettingsRow {
        id: valueRow
        host: dialog
        TextField {
            objectName: "setting_" + valueRow.setting
            readOnly: true
            selectByMouse: true
            text: {
                dialog.changedIds
                const v = dialog.settings ? dialog.settings.value(valueRow.setting) : ""
                return Array.isArray(v) ? v.join(", ") : String(v ?? "")
            }
            Layout.fillWidth: true
            Accessible.name: valueRow.text
        }
        IconTextButton {
            iconRole: "folder-open"
            text: qsTr("Open settings file")
            onClicked: dialog.openSettingsFile(valueRow.setting)
        }
    }
    // A setting with no options binding, written straight to settings.toml.
    component StoreCheck: SettingsRow {
        id: storeCheck
        host: dialog
        inline: true
        CheckBox {
            objectName: "setting_" + storeCheck.setting
            text: storeCheck.text
            leftPadding: 1
            spacing: 9
            Accessible.name: text
            Accessible.description: storeCheck.note
            checked: {
                dialog.changedIds
                return dialog.settings ? dialog.settings.value(storeCheck.setting) === true : false
            }
            onToggled: {
                dialog.settings.setValue(storeCheck.setting, checked)
                dialog.refreshChanged()
            }
        }
    }
    component StoreText: SettingsRow {
        id: storeText
        property string placeholder
        host: dialog
        TextField {
            id: storeField
            objectName: "setting_" + storeText.setting
            placeholderText: storeText.placeholder
            Layout.preferredWidth: 260
            Accessible.name: storeText.text
            function refresh() { text = dialog.settings ? dialog.settings.value(storeText.setting) ?? "" : "" }
            function commit() {
                storeTyping.stop()
                if (dialog.settings.value(storeText.setting) !== text.trim()) {
                    dialog.settings.setValue(storeText.setting, text.trim())
                    dialog.refreshChanged()
                }
            }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { if (!storeField.activeFocus) storeField.refresh() } }
            onTextEdited: storeTyping.restart()
            onEditingFinished: commit()
            Timer {
                id: storeTyping
                interval: 500
                onTriggered: storeField.commit()
            }
        }
    }

    ColumnLayout {
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 960
        Layout.preferredHeight: 560
        Layout.minimumWidth: 720
        Layout.minimumHeight: 360

        Rectangle {
            color: Theme.panel
            Layout.fillWidth: true
            implicitHeight: 40
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                TextField {
                    id: searchField
                    objectName: "settingsSearch"
                    placeholderText: qsTr("Search settings")
                    leftPadding: 30
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Search settings")
                    onTextChanged: searchSettle.restart()
                    Keys.onEscapePressed: event => {
                        if (text.length === 0) {
                            event.accepted = false
                            return
                        }
                        text = ""
                    }
                    Keys.onPressed: event => {
                        if (event.key !== Qt.Key_Backspace || selectedText.length > 0)
                            return
                        const before = text.slice(0, cursorPosition)
                        const token = before.match(/@[a-z]+(:[^ ]*)? ?$/)
                        if (!token)
                            return
                        const at = cursorPosition - token[0].length
                        text = text.slice(0, at) + text.slice(cursorPosition)
                        cursorPosition = at
                        event.accepted = true
                    }
                    Icon {
                        iconRole: "search"
                        color: Theme.muted
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                IconToolButton {
                    id: filterButton
                    objectName: "settingsFilter"
                    iconRole: "filter"
                    text: qsTr("Filter")
                    onClicked: filterMenu.popup(filterButton, 0, filterButton.height)
                }
                ShellMenu {
                    id: filterMenu
                    ShellMenuItem {
                        objectName: "settingsFilterModified"
                        iconRole: "document-modified"
                        text: qsTr("Modified")
                        onTriggered: dialog.addToken("@modified")
                    }
                    ShellMenu {
                        objectName: "settingsFilterPlugin"
                        iconRole: "plugins"
                        title: qsTr("Plugin")
                        enabled: dialog.pluginsWithSettings.length > 0
                        Repeater {
                            model: dialog.pluginsWithSettings
                            ShellMenuItem {
                                required property var modelData
                                iconRole: "plugins"
                                text: modelData.name
                                onTriggered: dialog.addToken("@plugin:" + modelData.id)
                            }
                        }
                    }
                }
                IconTextButton {
                    objectName: "settingsOpenFile"
                    iconRole: "folder-open"
                    text: qsTr("Open settings file")
                    onClicked: dialog.openSettingsFile("")
                }
            }
        }
        Timer {
            id: searchSettle
            interval: 150
            onTriggered: {
                dialog.query = searchField.text
                if (dialog.searching)
                    searchField.Accessible.announce(dialog.found.count === 0 ? noResults.text : qsTr("%n setting(s) match", "", dialog.found.count))
            }
        }
        TabBar {
            id: scopeTabs
            objectName: "settingsScope"
            visible: dialog.hasProject
            currentIndex: dialog.projectTab ? 1 : 0
            Layout.leftMargin: 12
            Layout.preferredHeight: 30
            TabButton {
                objectName: "settingsScopeYours"
                text: qsTranslate("HikariSub.Project", "Yours")
                width: implicitWidth
                onClicked: dialog.projectTab = false
            }
            TabButton {
                objectName: "settingsScopeProject"
                text: qsTranslate("HikariSub.Project", "Project: %1").arg(dialog.hasProject ? dialog.app.projectName(dialog.shellWindow) : "")
                width: implicitWidth
                onClicked: dialog.projectTab = true
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
        RowLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                color: Theme.background
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                ListView {
                    id: tree
                    objectName: "settingsPages"
                    anchors.fill: parent
                    anchors.rightMargin: 1
                    topMargin: 8
                    clip: true
                    model: dialog.treeRows
                    activeFocusOnTab: true
                    currentIndex: dialog.treeRows.findIndex(r => r.id === dialog.currentNode)
                    Accessible.role: Accessible.Tree
                    Accessible.name: qsTr("Settings contents")
                    Keys.onPressed: event => {
                        const r = dialog.treeRows[currentIndex]
                        if (!r)
                            return
                        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                            const next = Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                            dialog.currentNode = dialog.treeRows[next].id
                        } else if (event.key === Qt.Key_Right && r.depth === 0 && r.folded) {
                            dialog.unfold(r.id)
                        } else if (event.key === Qt.Key_Left && r.depth === 0 && !r.folded) {
                            dialog.toggleFold(r.id)
                        } else if (event.key === Qt.Key_Left && r.depth === 1) {
                            dialog.currentNode = r.id.split(".")[0]
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            dialog.showNode(r.id)
                        } else {
                            return
                        }
                        event.accepted = true
                    }
                    delegate: ItemDelegate {
                        id: node
                        required property var modelData
                        required property int index
                        objectName: modelData.depth === 0 ? "settingsPage_" + modelData.id : "settingsSection_" + modelData.id
                        width: ListView.view.width
                        height: 24
                        highlighted: ListView.isCurrentItem
                        text: modelData.name
                        leftPadding: 8 + 12 * modelData.depth
                        rightPadding: 8
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.showNode(modelData.id)
                        Accessible.role: Accessible.TreeItem
                        Accessible.name: text
                        Accessible.selected: highlighted
                        Accessible.expandable: modelData.foldable
                        Accessible.expanded: !modelData.folded
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            enabled: node.modelData.depth === 0 && node.modelData.id !== "plugins" && !dialog.projectTab
                            onTapped: { pageMenu.page = node.modelData.id; pageMenu.popup() }
                        }
                        background: Rectangle {
                            color: node.highlighted ? Theme.select : node.hovered ? Theme.raised : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 6
                            Icon {
                                visible: node.modelData.foldable
                                iconRole: node.modelData.folded ? "chevron-right" : "chevron-down"
                                TapHandler { onTapped: dialog.toggleFold(node.modelData.id) }
                            }
                            Item { visible: !node.modelData.foldable && node.modelData.depth === 0; implicitWidth: 16 }
                            Icon {
                                objectName: "settingsPageIcon"
                                visible: node.modelData.depth === 0
                                iconRole: node.modelData.icon
                            }
                            Label {
                                text: node.text
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: node.modelData.count >= 0
                                text: node.modelData.count
                                color: Theme.muted
                            }
                        }
                    }
                }
                ShellMenu {
                    id: pageMenu
                    property string page
                    ShellMenuItem {
                        objectName: "settingsResetPage"
                        iconRole: "restore-default"
                        text: qsTr("Reset page…")
                        onTriggered: dialog.askResetPage(pageMenu.page)
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.line
                }
            }
            Rectangle {
                color: Theme.panel
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollView {
                    id: rowsView
                    objectName: "settingsRows"
                    anchors.fill: parent
                    // MUST KILL: narrowest is copied in later because binding to the column's minimum width loops
                    property real narrowest: 0
                    contentWidth: Math.max(availableWidth, narrowest)
                    contentHeight: rowsColumn.implicitHeight + 48
                    Connections {
                        target: rowsView.contentItem
                        function onContentYChanged() { dialog.followScroll() }
                    }
                    Rectangle {
                        id: rowFlash
                        objectName: "settingsRowFlash"
                        property Item row: null
                        readonly property bool lit: dialog.flashSetting.length > 0
                        function offset(axis) {
                            let at = 0
                            for (let item = row; item && item !== rowsColumn; item = item.parent)
                                at += item[axis]
                            return at
                        }
                        visible: row !== null
                        x: rowsColumn.x + offset("x") - 8
                        y: rowsColumn.y + offset("y") - 4
                        width: row ? row.width + 16 : 0
                        height: row ? row.height + 8 : 0
                        radius: 2
                        color: lit ? Theme.select : "transparent"
                        border.width: lit && Theme.highContrast ? 1 : 0
                        border.color: Theme.accent
                        Behavior on color { enabled: !rowFlash.lit; ColorAnimation { duration: 2000 } }
                    }
                    ColumnLayout {
                        id: rowsColumn
                        x: 24
                        y: 12
                        width: rowsView.contentWidth - 48
                        spacing: 24
                        onImplicitWidthChanged: Qt.callLater(() => rowsView.narrowest = Layout.minimumWidth + 48)
                        InlineBar {
                            objectName: "settingsFileProblem"
                            visible: !dialog.projectTab && dialog.settings !== null && dialog.settings.fileProblem.length > 0
                            severity: "error"
                            closable: false
                            text: dialog.settings ? dialog.settings.fileProblem + " " + qsTr("HikariSub keeps the last values it could read, and your changes here wait until it reads again.") : ""
                            Layout.fillWidth: true
                        }
                        InlineBar {
                            objectName: "settingsLandingNote"
                            visible: dialog.landingNote.length > 0
                            severity: "info"
                            text: dialog.landingNote
                            onClosed: dialog.landingNote = ""
                            Layout.fillWidth: true
                        }
                        Label {
                            id: noResults
                            objectName: "settingsNoResults"
                            visible: dialog.searching && dialog.found.count === 0
                            text: dialog.found.modified && dialog.query.replace("@modified", "").trim().length === 0
                                  ? qsTr("Nothing is changed here.")
                                  : qsTr("No settings match “%1”.").arg(dialog.query.trim())
                            color: Theme.muted
                        }
                        ProjectSettingsPages {
                            id: projectPages
                            dialog: dialog
                            visible: dialog.projectTab
                            Layout.fillWidth: true
                        }
                        ColumnLayout {
                            id: yoursPages
                            visible: !dialog.projectTab
                            spacing: 24
                            Layout.fillWidth: true
                SettingsPage {
                    page: "general"
                    objectName: "settingsPageGeneral"
                    Section {
                        key: "general.language"
                        objectName: "settingsLanguageGroup"
                        ruled: false
                        SettingsRow {
                            host: dialog
                            setting: "program.language"
                            SettingChoice {
                                setting: "program.language"
                                model: dialog.languages
                                Accessible.name: qsTr("Interface language")
                            }
                        }
                    }
                    Section {
                        key: "general.tabs"
                        SettingCheck { setting: "subtitles.openInNewTab" }
                        SettingsRow {
                            host: dialog
                            setting: "program.tabTextMaxChars"
                            SettingNumber {
                                setting: "program.tabTextMaxChars"; from: 20; to: 150; unit: qsTr("characters")
                                name: qsTr("Tab name length"); tip: qsTr("Number of tab name characters. Range from 20 to 150")
                            }
                        }
                    }
                    Section {
                        key: "general.saving"
                        SettingsRow {
                            host: dialog
                            setting: "autosave.maxFiles"
                            SettingNumber {
                                setting: "autosave.maxFiles"; from: 2; to: 1000000; name: qsTr("Autosave files kept")
                                tip: qsTr("Number of autosaves can be set from 2 to 1000000")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "autosave.intervalSeconds"
                            SettingNumber {
                                setting: "autosave.intervalSeconds"; from: 5; to: 3600; unit: qsTr("seconds")
                                name: qsTr("Autosave every"); tip: qsTr("Seconds from a change to its autosave. Range from 5 to 3600")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "autosave.savedCopies"
                            SettingNumber {
                                setting: "autosave.savedCopies"; from: 0; to: 100; name: qsTr("Backup copies kept")
                                tip: qsTr("Earlier saves of each file kept in the Recovery folder. Range from 0 to 100")
                            }
                        }
                    }
                    Section {
                        key: "general.notices"
                        objectName: "settingsNoticesGroup"
                        SettingCheck { setting: "notices.toasts" }
                        SettingsRow {
                            text: qsTr("Questions hidden with “Don’t ask again”")
                            IconTextButton {
                                iconRole: "notices"
                                objectName: "settingsAskAgain"
                                text: qsTr("Ask again")
                                enabled: (dialog.values["notices.hiddenQuestions"] || []).length > 0
                                onClicked: dialog.put("notices.hiddenQuestions", [])
                            }
                        }
                    }
                    Section {
                        key: "general.folders"
                        SettingsRow {
                            objectName: "settingsUserFolderRow"
                            visible: (dialog.app.userFolder || "").length > 0
                            text: qsTr("User folder")
                            TextField {
                                objectName: "settingsUserFolderPath"
                                text: dialog.app.userFolder || ""
                                readOnly: true
                                selectByMouse: true
                                Layout.fillWidth: true
                                Accessible.name: qsTr("User folder")
                            }
                            IconTextButton {
                                iconRole: "show-in-folder"
                                objectName: "settingsOpenUserFolder"
                                text: qsTr("Open")
                                Accessible.name: qsTr("Open user folder")
                                onClicked: dialog.app.openUserFolder()
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "fonts.externalDirectory"
                            SettingText { id: fontsFolder; setting: "fonts.externalDirectory"; Accessible.name: qsTr("Fonts folder") }
                            IconTextButton {
                                iconRole: "folder-open"
                                text: qsTr("Choose…")
                                Accessible.name: qsTr("Choose external font folder")
                                onClicked: fontsFolderDialog.open()
                            }
                        }
                    }
                    Section {
                        key: "general.trustedFolders"
                        objectName: "settingsTrustedFolders"
                        SettingNote { text: qsTr("HikariSub trusts these folders and the folders inside them: scripts a subtitle file names load without asking, and a file inside a Project opens the Project.") }
                        Label {
                            objectName: "settingsTrustedFoldersEmpty"
                            visible: (dialog.values["trust.folders"] || []).length === 0
                            text: qsTr("No trusted folders")
                            color: Theme.muted
                            Layout.leftMargin: 24
                        }
                        Repeater {
                            model: dialog.values["trust.folders"] || []
                            delegate: RowLayout {
                                id: trustedRow
                                required property string modelData
                                spacing: 8
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                Label {
                                    text: trustedRow.modelData
                                    textFormat: Text.PlainText
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    ToolTip.text: trustedRow.modelData
                                    ToolTip.visible: truncated && trustedHover.hovered
                                    HoverHandler { id: trustedHover }
                                }
                                Button {
                                    objectName: "settingsUntrustFolder"
                                    text: qsTr("Remove")
                                    Accessible.description: qsTr("Stop trusting %1").arg(trustedRow.modelData)
                                    onClicked: dialog.put("trust.folders",
                                                          (dialog.values["trust.folders"] || [])
                                                              .filter(f => f !== trustedRow.modelData))
                                }
                            }
                        }
                    }
                    Section {
                        key: "general.legacy"
                        objectName: "settingsLegacyGroup"
                        SettingsRow {
                            text: qsTr("HikariSub 0.0.x")
                            IconTextButton {
                                iconRole: "import"
                                objectName: "settingsImportLegacy"
                                text: qsTr("Import settings…")
                                enabled: dialog.importer !== null && dialog.importer.available
                                onClicked: dialog.legacyImportRequested()
                            }
                            IconTextButton {
                                iconRole: "undo"
                                objectName: "settingsRollBackImport"
                                visible: dialog.importer !== null && dialog.importer.canRollBack
                                text: qsTr("Roll back the last import…")
                                onClicked: dialog.legacyRollBackRequested()
                            }
                        }
                        SettingNote {
                            objectName: "settingsLegacyNotFound"
                            visible: dialog.legacyNotFound.length > 0
                            text: dialog.legacyNotFound
                        }
                        SettingsRow {
                            text: qsTr("Script shortcuts")
                            IconTextButton {
                                objectName: "hotkeyImportLegacy"
                                iconRole: "import"
                                text: qsTr("Import legacy shortcuts…")
                                enabled: dialog.hotkeys !== null
                                tip: qsTr("Adds the script shortcuts of a legacy Hotkeys.txt at once")
                                onClicked: legacyHotkeysFile.open()
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "appearance"
                    objectName: "settingsPageAppearance"
                    AppearancePage { dialog: dialog }
                }

                SettingsPage {
                    page: "lineEditor"
                    objectName: "settingsPageLineEditor"
                    Section {
                        key: "lineEditor.display"
                        ruled: false
                        SettingCheck { setting: "editor.hideTags" }
                        SettingCheck { setting: "editor.timesToFramesSwitch" }
                    }
                    Section {
                        key: "lineEditor.editing"
                        SettingCheck { setting: "editor.dontGoToNextLineOnTimesEdit" }
                        SettingCheck { setting: "editor.allowNumpadHotkeys" }
                        SettingCheck { setting: "editor.softLineBreak"; text: qsTr("Shift+Enter inserts a soft line break (%1)").arg("\\n") }
                    }
                    Section {
                        key: "lineEditor.translationMode"
                        SettingCheck { setting: "translation.showOriginal" }
                        SettingCheck { setting: "translation.hideOriginalOnVideo" }
                        StoreCheck { setting: "translation.autoMoveTagsFromOriginal" }
                        StoreText { setting: "translation.sourceLanguage"; placeholder: qsTranslate("HikariSub.Project", "Detect") }
                        StoreText { setting: "translation.targetLanguage" }
                        ValueRow { setting: "translation.glossary" }
                    }
                    Section {
                        key: "lineEditor.tagButtons"
                        objectName: "settingsTagButtons"
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
                                    parent: tagButtonRows // a ListView's children go into its content item
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

                SettingsPage {
                    page: "editing"
                    objectName: "settingsPageEditing"
                    CycleValuesPage { dialog: dialog }
                }

                SettingsPage {
                    page: "spelling"
                    id: spellingPage
                    objectName: "settingsPageSpelling"
                    readonly property bool grammarOn: (dialog.values["grammar.language"] ?? 0) > 0
                    Section {
                        key: "spelling.spelling"
                        ruled: false
                        SettingCheck { setting: "editor.spellchecker" }
                        SettingsRow {
                            host: dialog
                            setting: "editor.dictionaryLanguage"
                            SettingChoice {
                                setting: "editor.dictionaryLanguage"
                                model: dialog.dictionaries
                                enabled: count > 0
                                displayText: currentIndex >= 0 ? currentText
                                           : count > 0 ? qsTr("None chosen") : qsTr("No dictionaries found")
                                Accessible.name: qsTr("Primary language")
                            }
                        }
                        SettingCheck { setting: "editor.suggestionsOnDoubleClick" }
                    }
                    Section {
                        key: "spelling.grammar"
                        SettingsRow {
                            host: dialog
                            setting: "grammar.language"
                            SettingChoice {
                                setting: "grammar.language"
                                model: [qsTr("Off"), qsTr("English (US)"), qsTr("English (UK)"), qsTr("English (Canada)"),
                                        qsTr("English (Australia)")]
                                Accessible.name: qsTr("Grammar check")
                            }
                        }
                        SettingNote {
                            visible: spellingPage.grammarOn && dialog.values["editor.spellchecker"] === true
                            text: qsTr("While spelling is checked, Harper leaves spelling to the dictionary above, so a word is never marked twice.")
                        }
                    }
                    Section {
                        key: "spelling.ruleGroups"
                        objectName: "settingsGrammarGroups"
                        SettingNote {
                            visible: !spellingPage.grammarOn
                            text: qsTr("Choose an English above to switch them.")
                            Layout.topMargin: -4
                        }
                        GridLayout {
                            columns: 2
                            columnSpacing: 24
                            rowSpacing: 8
                            Layout.leftMargin: 24
                            Layout.fillWidth: true
                            Repeater {
                                model: dialog.app.grammarGroups()
                                CheckBox {
                                    id: group
                                    required property var modelData
                                    readonly property string setting: "grammar.group." + modelData.id
                                    objectName: "setting_" + setting
                                    text: modelData.name
                                    enabled: spellingPage.grammarOn
                                    leftPadding: 1
                                    spacing: 9
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1 // the two columns share the width
                                    Layout.alignment: Qt.AlignTop
                                    Accessible.name: text
                                    Accessible.description: groupDescription.text
                                    function refresh() { checked = dialog.values[setting] === true }
                                    Component.onCompleted: refresh()
                                    Connections { target: dialog; function onReloaded() { group.refresh() } }
                                    onToggled: dialog.put(setting, checked)
                                    contentItem: ColumnLayout {
                                        spacing: 1
                                        RowLayout {
                                            spacing: 6
                                            Layout.leftMargin: group.indicator.width + group.spacing
                                            Label {
                                                text: group.text
                                                color: group.enabled ? Theme.text : Theme.muted
                                            }
                                            Label {
                                                text: qsTr("%n rules", "", group.modelData.rules)
                                                color: Theme.muted
                                                font.pixelSize: Math.round(group.font.pixelSize * 0.92)
                                            }
                                        }
                                        Label {
                                            id: groupDescription
                                            text: group.modelData.description
                                            color: Theme.muted
                                            wrapMode: Text.Wrap
                                            Layout.leftMargin: group.indicator.width + group.spacing
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            objectName: "overridden_" + group.setting
                                            visible: dialog.overrides.includes(group.setting)
                                            text: qsTranslate("HikariSub.Project", "Overridden in this project")
                                            color: Theme.muted
                                            Layout.leftMargin: group.indicator.width + group.spacing
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            objectName: "settingsGrammarLicence"
                            Layout.leftMargin: 24
                            Layout.topMargin: 6
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            wrapMode: Text.Wrap
                            color: Theme.muted
                            linkColor: Theme.accent
                            text: qsTr("Harper %1 by Automattic, Apache License 2.0. %2 in Help › Credits.")
                                  .arg(dialog.app.grammarVersion, "<a href=\"credits\">" + qsTr("Licence notice") + "</a>")
                            onLinkActivated: dialog.creditsRequested()
                            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        }
                    }
                }

                SettingsPage {
                    page: "grid"
                    objectName: "settingsPageGrid"
                    Section {
                        key: "grid.activeLine"
                        ruled: false
                        SettingCheck { setting: "grid.changeActiveOnSelection" }
                        SettingCheck { setting: "grid.duplicationDontChangeSelection" }
                        SettingCheck { setting: "grid.setVisibleLineAfterFullScreen" }
                    }
                    Section {
                        key: "grid.columns"
                        SettingCheck { setting: "grid.autoHideColumns" }
                    }
                    Section {
                        key: "grid.display"
                        SettingsRow {
                            host: dialog
                            setting: "grid.tagsMode"
                            SettingChoice {
                                setting: "grid.tagsMode"
                                model: [qsTr("Show"), qsTr("Replace with symbol"), qsTr("Hide")]
                                Accessible.name: qsTr("Tags in the Grid")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "grid.tagsSwapCharacter"
                            SettingText { setting: "grid.tagsSwapCharacter"; Layout.preferredWidth: 120; Accessible.name: qsTr("ASS tag replacement") }
                        }
                        SettingCheck { setting: "grid.markCurrentFrame" }
                    }
                    Section {
                        key: "grid.counting"
                        SettingCheck { setting: "grid.calcSpacesAndPunctuationForWraps" }
                        SettingCheck { setting: "grid.calcSpacesAndPunctuationForCps" }
                        SettingsRow {
                            host: dialog
                            setting: "grid.cpsWarning"
                            SettingNumber { setting: "grid.cpsWarning"; from: 1; to: 100; unit: qsTr("CPS") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "grid.cpsError"
                            SettingNumber { setting: "grid.cpsError"; from: 1; to: 100; unit: qsTr("CPS") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "grid.lineLength"
                            SettingNumber { setting: "grid.lineLength"; from: 1; to: 1000 }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "grid.maxLines"
                            SettingNumber { setting: "grid.maxLines"; from: 1; to: 20 }
                        }
                        SettingCheck { setting: "problems.reading" }
                    }
                    Section {
                        key: "grid.splitting"
                        SettingCheck { setting: "grid.splitFramesBakeAnimations" }
                        SettingCheck { setting: "grid.splitKaraokeKeepLayout" }
                    }
                    Section {
                        key: "grid.newLines"
                        SettingsRow {
                            host: dialog
                            setting: "grid.insertStartOffset"
                            SettingNumber { setting: "grid.insertStartOffset"; from: -100000; to: 100000; unit: qsTr("ms"); name: qsTr("Start frame offset") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "grid.insertEndOffset"
                            SettingNumber { setting: "grid.insertEndOffset"; from: -100000; to: 100000; unit: qsTr("ms"); name: qsTr("End frame offset") }
                        }
                    }
                }

                SettingsPage {
                    page: "video"
                    objectName: "settingsPageVideo"
                    Section {
                        key: "video.fullscreen"
                        objectName: "settingsFullscreen"
                        ruled: false
                        SettingsRow {
                            host: dialog
                            setting: "video.fullscreenBarPosition"
                            iconRole: "fullscreen"
                            SettingChoice {
                                setting: "video.fullscreenBarPosition"
                                model: [qsTr("Below the picture (letterbox)"), qsTr("Over the picture")]
                                Accessible.name: qsTr("Bar position")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.fullscreenBar"
                            iconRole: "show-toolbar"
                            SettingChoice {
                                setting: "video.fullscreenBar"
                                model: [qsTr("Auto-hide"), qsTr("Always"), qsTr("Never")]
                                Accessible.name: qsTr("Show bar")
                            }
                        }
                        SettingCheck { setting: "video.fullscreenHidePointer" }
                        SettingCheck { setting: "video.fullscreenOverlays" }
                        SettingsRow {
                            host: dialog
                            setting: "video.fullscreenOpensIn"
                            SettingChoice {
                                setting: "video.fullscreenOpensIn"
                                model: [qsTr("Simple"), qsTr("Typesetting")]
                                Accessible.name: qsTr("Opens in")
                            }
                        }
                    }
                    Section {
                        key: "video.playback"
                        SettingCheck { setting: "video.pauseOnClick" }
                        SettingCheck { setting: "video.fullScreenOnStart" }
                        SettingCheck { setting: "video.openAtActiveLine" }
                        SettingsRow {
                            host: dialog
                            setting: "video.moveToActiveLine"
                            text: dialog.follow.seekName
                            SettingChoice {
                                setting: "video.moveToActiveLine"
                                model: dialog.follow.seekChoices
                                Accessible.name: dialog.follow.seekName
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.playAfterSelection"
                            text: dialog.follow.playName
                            SettingChoice {
                                setting: "video.playAfterSelection"
                                model: dialog.follow.playChoices
                                Accessible.name: dialog.follow.playName
                            }
                        }
                        Loader {
                            // MUST KILL: docsEntries is read by name by the docs export, active or not
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
                    Section {
                        key: "video.subtitlesOnVideo"
                        SettingsRow {
                            host: dialog
                            setting: "video.subtitleProvider"
                            objectName: "settingsRendererGroup"
                            visible: dialog.renderers.length > 1
                            SettingChoice {
                                setting: "video.subtitleProvider"
                                model: dialog.rendererLabels
                                Accessible.name: qsTr("Subtitle display filter")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.previewSubtitlesAt"
                            id: previewAtRow
                            objectName: "settingsPreviewSubtitlesAt"
                            readonly property bool libass: dialog.renderers.length < 2
                                || dialog.renderers[dialog.values["video.subtitleProvider"] ?? -1] === "libass"
                            note: libass ? qsTr("Screen size draws subtitles at the size the picture takes on screen, so they stay sharp on large and high-DPI screens. The script’s layout is the same either way. Also used by the previews in the Style manager, the font picker and the Template editor.")
                                         : qsTr("Video resolution only with this filter.")
                            SettingChoice {
                                setting: "video.previewSubtitlesAt"
                                enabled: previewAtRow.libass
                                model: [qsTr("Video resolution (as encoded)"), qsTr("Screen size (sharp)")]
                                Accessible.name: qsTr("Preview subtitles at")
                            }
                        }
                        SettingCheck { setting: "video.disableLiveEditing" }
                    }
                    Section {
                        key: "video.decoding"
                        SettingsRow {
                            host: dialog
                            setting: "video.ffms2Seeking"
                            SettingChoice {
                                setting: "video.ffms2Seeking"
                                model: [qsTr("Linear"), qsTr("Normal"), qsTr("Unsafe (always fast)"), qsTr("Aggressive (fast in rewind)")]
                                Accessible.name: qsTr("Seeking method")
                            }
                        }
                        SettingCheck { setting: "video.gpuConversion" }
                        SettingsRow {
                            host: dialog
                            setting: "video.acceptedAudioStream"
                            SettingText { setting: "video.acceptedAudioStream"; Accessible.name: qsTr("Preferred audio") }
                        }
                    }
                    Section {
                        key: "video.view"
                        SettingsRow {
                            host: dialog
                            setting: "video.zoomModel"
                            id: zoomModelRow
                            objectName: "settingsZoomModel"
                            readonly property bool aegisub: (dialog.values["video.zoomModel"] ?? 0) === 0
                            note: aegisub ? qsTr("Middle-drag pans. A touchpad pinch zooms around the pointer.")
                                          : qsTr("The wheel zooms around the pointer. Zoom video shows a frame to drag.")
                            SettingChoice {
                                setting: "video.zoomModel"
                                model: [qsTr("As in Aegisub 3.5"), qsTr("As in HikariSub legacy")]
                                Accessible.name: qsTr("Zoom and pan")
                            }
                        }
                        Repeater {
                            model: [
                                { setting: "video.scrollAction", text: qsTr("Wheel on the video") },
                                { setting: "video.ctrlScrollAction", text: qsTr("Ctrl+wheel on the video") },
                                { setting: "video.shiftScrollAction", text: qsTr("Shift+wheel on the video") }
                            ]
                            SettingsRow {
                                required property var modelData
                                objectName: "settingsScroll_" + modelData.setting
                                text: modelData.text
                                enabled: zoomModelRow.aegisub
                                SettingChoice {
                                    setting: modelData.setting
                                    model: [qsTr("Zooms the video"), qsTr("Zooms the video (reversed)"), qsTr("Pans the video"),
                                            qsTr("Pans the video (X and Y swapped)"), qsTr("Does nothing")]
                                    Accessible.name: modelData.text
                                }
                            }
                        }
                        SettingNote {
                            visible: zoomModelRow.aegisub
                            text: qsTr("A visual tool that uses the wheel, or drags with the middle button, keeps it.")
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.zoomPercent"
                            // MUST KILL: video.zoomPercent is a number edited as text
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
                    Section {
                        key: "video.visualTools"
                        SettingsRow {
                            host: dialog
                            setting: "video.handleSize"
                            SettingNumber {
                                setting: "video.handleSize"; from: 50; to: 400; unit: "%"
                                name: qsTr("Handle size"); tip: qsTr("Size of the visual tools’ handles. Range from 50 to 400")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.vectorClipSmoothing"
                            SettingNumber {
                                setting: "video.vectorClipSmoothing"; from: 0; to: 100
                                name: qsTr("Vector clip smoothing"); tip: qsTr("How closely a freehand stroke is followed. Range from 0 to 100")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "video.drawingSmoothing"
                            SettingNumber {
                                setting: "video.drawingSmoothing"; from: 0; to: 100
                                name: qsTr("Drawing smoothing"); tip: qsTr("How closely a freehand stroke is followed. Range from 0 to 100")
                            }
                        }
                    }
                    Section {
                        key: "video.scriptResolution"
                        SettingCheck { setting: "video.visualWarningsOff" }
                        SettingCheck { setting: "video.dontAskForBadResolution" }
                        SettingCheck { setting: "automation.scriptResolutionFromVideo" }
                        Repeater {
                            model: [
                                { setting: "video.missingMatrix", text: qsTr("No YCbCr matrix in the subtitles"),
                                  choices: [qsTr("Leave it unset"), qsTr("Ask"), qsTr("Set it to the video’s")] },
                                { setting: "video.matrixMismatch", text: qsTr("A different YCbCr matrix from the video’s"),
                                  choices: [qsTr("Keep the subtitles’"), qsTr("Ask"), qsTr("Set it to the video’s"), qsTr("Convert the colours and set it to the video’s")] },
                                { setting: "video.missingLayoutRes", text: qsTr("No layout resolution in the subtitles"),
                                  choices: [qsTr("Leave it unset"), qsTr("Ask"), qsTr("Set it to the video’s"), qsTr("Set it to the script resolution")] },
                                { setting: "video.layoutResMismatch", text: qsTr("A different layout resolution from the video’s"),
                                  choices: [qsTr("Never ask"), qsTr("Ask when the aspect ratio differs"), qsTr("Always ask")] }
                            ]
                            SettingsRow {
                                required property var modelData
                                text: modelData.text
                                SettingChoice {
                                    setting: modelData.setting
                                    model: modelData.choices
                                    Accessible.name: modelData.text
                                }
                            }
                        }
                        SettingNote { text: qsTr("Checked when a video opens.") }
                        SettingCheck { setting: "video.untaggedMatrixWarning" }
                        SettingCheck { setting: "video.hdrWarning" }
                    }
                }

                SettingsPage {
                    page: "audio"
                    objectName: "settingsPageAudio"
                    Section {
                        key: "audio.output"
                        objectName: "audioOutputBox"
                        ruled: false
                        SettingsRow {
                            host: dialog
                            setting: "audio.outputDevice"
                            SettingChoice {
                                setting: "audio.outputDevice"
                                model: dialog.outputDevices
                                Accessible.name: qsTr("Device")
                            }
                        }
                        Loader {
                            objectName: "audioOutputHostApiBox"
                            // MUST KILL: docsEntries is read by name by the docs export, active or not
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
                    Section {
                        key: "audio.playback"
                        SettingsRow {
                            host: dialog
                            setting: "audio.delay"
                            SettingNumber { setting: "audio.delay"; from: -50000000; to: 50000000; unit: qsTr("ms"); name: qsTr("Audio delay") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "audio.markPlayTime"
                            SettingNumber { setting: "audio.markPlayTime"; from: 400; to: 5000; unit: qsTr("ms"); name: qsTr("Play around the marker") }
                        }
                        SettingsRow {
                            text: qsTr("Lead-in and lead-out")
                            SettingNumber { setting: "audio.leadInValue"; from: 0; to: 10000; unit: qsTr("ms"); name: qsTr("Lead-in") }
                            SettingNumber { setting: "audio.leadOutValue"; from: 0; to: 10000; unit: qsTr("ms"); name: qsTr("Lead-out") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "audio.defaultDuration"
                            SettingNumber { setting: "audio.defaultDuration"; from: 100; to: 60000; unit: qsTr("ms"); name: qsTr("Default line duration") }
                        }
                        SettingCheck { setting: "audio.lockScrollOnCursor" }
                        SettingCheck { setting: "audio.dontPlayWhenLineChanges" }
                        SettingCheck { setting: "audio.autoFocus" }
                        SettingsRow {
                            host: dialog
                            setting: "audio.timingKeysEverywhere"
                            SettingChoice {
                                setting: "audio.timingKeysEverywhere"
                                model: [qsTr("Never"), qsTr("In the Timing layout"), qsTr("Always")]
                                Accessible.name: qsTr("Timing keys work in every panel")
                            }
                        }
                        SettingCheck { setting: "audio.playMono" }
                    }
                    Section {
                        key: "audio.display"
                        SettingCheck { setting: "audio.drawTimeCursor" }
                        SettingCheck { setting: "audio.drawSecondaryLines" }
                        SettingCheck { setting: "audio.drawSelectionBackground" }
                        SettingCheck { setting: "audio.drawVideoPosition" }
                        SettingCheck { setting: "audio.drawKeyframes" }
                        SettingsRow {
                            host: dialog
                            setting: "audio.lineBoundariesThickness"
                            SettingNumber { setting: "audio.lineBoundariesThickness"; from: 1; to: 5; name: qsTr("Line boundaries thickness") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "audio.inactiveLinesDisplayMode"
                            SettingChoice {
                                setting: "audio.inactiveLinesDisplayMode"
                                model: [qsTr("None"), qsTr("Before and after the active"), qsTr("All visible")]
                                Accessible.name: qsTr("Inactive lines")
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "audio.spectrumCurve"
                            SettingChoice {
                                setting: "audio.spectrumCurve"
                                model: [qsTr("Linear"), qsTr("Speech"), qsTr("Logarithmic")]
                                Accessible.name: qsTr("Frequency curve")
                            }
                        }
                    }
                    Section {
                        key: "audio.snapping"
                        SettingCheck { setting: "audio.snapToKeyframes" }
                        SettingCheck { setting: "audio.snapToOtherLines" }
                    }
                    Section {
                        key: "audio.karaoke"
                        SettingCheck { setting: "audio.mergeEveryNWithSyllable" }
                        SettingCheck { setting: "audio.karaokeMoveOnClick" }
                        SettingCheck { setting: "audio.karaokeDoubleConsonantApart" }
                        SettingCheck {
                            setting: "audio.karaokeThaiClusters"
                            tip: qsTr("Auto split gives each Thai character cluster its own syllable, so กิน becomes กิ|น. Turn it off to keep each Thai word one syllable.")
                        }
                        SettingsRow {
                            host: dialog
                            setting: "audio.karaokeKfThreshold"
                            text: qsTr("Long syllables for %1").arg("\\kf")
                            SettingNumber {
                                setting: "audio.karaokeKfThreshold"; from: 10; to: 10000; unit: qsTr("cs"); name: qsTr("Long syllables for %1").arg("\\kf")
                                tip: qsTr("Switch long syllables between %1 and %2 changes the syllables at least this long.").arg("\\k", "\\kf")
                            }
                        }
                    }
                    Section {
                        key: "audio.cache"
                        SettingCheck { setting: "audio.ramCache" }
                        SettingsRow {
                            host: dialog
                            setting: "audio.cacheFilesLimit"
                            note: qsTr("0 keeps them all.")
                            SettingNumber {
                                setting: "audio.cacheFilesLimit"; from: 0; to: 10000; unit: qsTr("files"); name: qsTr("Cache files kept")
                                tip: qsTr("0–10000. At 0, audio cache files are never removed.")
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "conversion"
                    objectName: "settingsPageConversion"
                    Section {
                        key: "conversion.style"
                        ruled: false
                        SettingsRow {
                            host: dialog
                            setting: "convert.styleCatalog"
                            SettingChoice {
                                setting: "convert.styleCatalog"
                                model: dialog.catalogs
                                Accessible.name: qsTr("Catalogue")
                                onChosen: index => dialog.chooseCatalog(index)
                            }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "convert.style"
                            SettingChoice {
                                setting: "convert.style"
                                model: dialog.styles
                                Accessible.name: qsTr("Style")
                            }
                        }
                    }
                    Section {
                        key: "conversion.timing"
                        SettingsRow {
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
                        SettingCheck { setting: "convert.fpsFromVideo" }
                        SettingCheck { setting: "convert.newEndTimes" }
                        SettingsRow {
                            host: dialog
                            setting: "convert.timePerCharacter"
                            SettingNumber { setting: "convert.timePerCharacter"; from: 30; to: 1000; unit: qsTr("ms"); name: qsTr("Time per letter") }
                        }
                    }
                    Section {
                        key: "conversion.ass"
                        SettingsRow {
                            text: qsTr("Resolution")
                            SettingNumber { setting: "convert.resolutionWidth"; from: 1; to: 3000; name: qsTr("Width") }
                            Label { text: "×"; color: Theme.muted }
                            SettingNumber { setting: "convert.resolutionHeight"; from: 1; to: 3000; name: qsTr("Height") }
                        }
                        SettingsRow {
                            host: dialog
                            setting: "convert.assTagsToInsertInLine"
                            SettingText { setting: "convert.assTagsToInsertInLine"; Accessible.name: qsTr("Tags at the start of each line") }
                        }
                    }
                    Section {
                        key: "conversion.export"
                        SettingsRow {
                            host: dialog
                            setting: "export.format"
                            ComboBox {
                                id: exportFormat
                                objectName: "setting_export.format"
                                model: projectPages.exportFormats
                                textRole: "text"
                                Layout.preferredWidth: 260
                                Accessible.name: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.label("export.format") : "")
                                currentIndex: {
                                    dialog.changedIds
                                    const v = dialog.settings ? dialog.settings.value("export.format") : ""
                                    return Math.max(0, projectPages.exportFormats.findIndex(f => f.id === v))
                                }
                                onActivated: index => {
                                    dialog.settings.setValue("export.format", projectPages.exportFormats[index].id)
                                    dialog.refreshChanged()
                                }
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "subtitleProperties"
                    objectName: "settingsPageSubtitleProperties"
                    Section {
                        key: "subtitleProperties.credits"
                        ruled: false
                        Label {
                            text: qsTranslate("HikariSub.Project", "Saving a file writes the switched-on fields into its Script Info.")
                            color: Theme.muted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: projectPages.scriptFields
                            CreditRow {}
                        }
                        SettingCheck {
                            setting: "scriptProperties.askForChange"
                        }
                    }
                    Section {
                        key: "subtitleProperties.files"
                        SettingCheck { setting: "subtitles.layoutInFile" }
                        SettingsRow {
                            host: dialog
                            setting: "new.styleCatalog"
                            ComboBox {
                                id: newStyleCatalog
                                objectName: "setting_new.styleCatalog"
                                model: dialog.newFileCatalogs
                                textRole: "text"
                                Layout.preferredWidth: 260
                                Accessible.name: (SettingsCatalogue.revision >= 0 ? SettingsCatalogue.label("new.styleCatalog") : "")
                                currentIndex: {
                                    dialog.changedIds
                                    const v = dialog.settings ? dialog.settings.value("new.styleCatalog") : ""
                                    return Math.max(0, dialog.newFileCatalogs.findIndex(c => c.value === v))
                                }
                                onActivated: index => {
                                    dialog.settings.setValue("new.styleCatalog", dialog.newFileCatalogs[index].value)
                                    dialog.refreshChanged()
                                }
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "automation"
                    objectName: "settingsPageAutomation"
                    Section {
                        key: "automation.debugging"
                        ruled: false
                        SettingsRow {
                            host: dialog
                            setting: "automation.traceLevel"
                            SettingNumber { setting: "automation.traceLevel"; from: 0; to: 5; name: qsTr("Lua trace level") }
                        }
                    }
                    Section {
                        key: "automation.scriptDialogs"
                        objectName: "settingsScriptDialogsGroup"
                        SettingCheck { setting: "automation.fillLastAnswers" }
                    }
                    Section {
                        key: "automation.keptAnswersAndPresets"
                        SettingsRow {
                            text: qsTr("Last answers")
                            note: qsTr("Presets are kept; delete them from a dialog’s ⋯ menu.")
                            IconTextButton {
                                iconRole: "clear"
                                objectName: "settingsClearAnswers"
                                text: qsTr("Clear last answers…")
                                enabled: (dialog.values["automation.dialogAnswers"] || []).length > 0
                                onClicked: dialog.clearLastAnswers()
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "hotkeys"
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(360, rowsView.height - 96)
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
                                    // MUST KILL: the area modes are HotkeyList's filter ints without 1, which Source replaced
                                    readonly property var modes: [0, 2, 3, 4, 5, 6, 7, 8]
                                    model: [qsTr("All"), qsTr("Global shortcuts"), qsTr("Grid shortcuts"),
                                            qsTr("Line editor shortcuts"), qsTr("Video shortcuts"), qsTr("Audio shortcuts"),
                                            qsTr("Automation shortcuts"), qsTr("Plugin shortcuts")]
                                    ToolTip.text: qsTr("Filtering mode")
                                    ToolTip.visible: hovered
                                    Accessible.name: qsTr("Choose filtering")
                                    onActivated: index => dialog.hotkeys.filter(modes[index])
                                }
                                ComboBox {
                                    id: hotkeySource
                                    objectName: "hotkeySource"
                                    Layout.preferredWidth: 140
                                    model: [qsTr("All"), qsTr("Built-in"), qsTranslate("HikariSub.Project", "Yours"), qsTr("Plugin"), qsTr("Macro")]
                                    ToolTip.text: qsTr("Source")
                                    ToolTip.visible: hovered
                                    Accessible.name: qsTr("Source")
                                    onActivated: index => dialog.hotkeys.filterSource(index)
                                }
                                CheckBox {
                                    id: hotkeyConflicts
                                    objectName: "hotkeyConflicts"
                                    text: qsTr("Conflicts")
                                    Accessible.name: text
                                    onToggled: dialog.hotkeys.filterConflicts(checked)
                                }
                                TextField {
                                    id: hotkeySearch
                                    objectName: "hotkeySearch"
                                    placeholderText: qsTr("Search shortcuts")
                                    leftPadding: 30
                                    Layout.fillWidth: true
                                    Accessible.name: qsTr("Search shortcuts")
                                    onTextChanged: if (dialog.hotkeys) dialog.hotkeys.search(text)
                                    Icon {
                                        iconRole: "search"
                                        color: Theme.muted
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
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
                                readonly property int selectedRow: dialog.hotkeys ? dialog.hotkeys.selected : -1
                                ScrollBar.vertical: ScrollBar {}
                                Accessible.name: qsTr("Shortcuts")
                                // MUST KILL: contexts is indexed by a magic window int, 5 standing for Automation
                                readonly property var contexts: [qsTr("Global"), qsTr("Grid"), qsTr("Line editor"),
                                                                 qsTr("Video"), qsTr("Audio"), qsTr("Automation")]
                                function contextOf(row) { return row.script !== undefined ? 5 : row.context }
                                readonly property bool gutter: dialog.hotkeys ? dialog.hotkeys.listFiltered && hotkeySearch.text.length === 0
                                                                              : false
                                component BlockBox: Label {
                                    // MUST KILL: block is a magic int (0 none, 1 rows hidden, else rows shown)
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
                                        Label { text: qsTr("Shortcut"); color: Theme.muted; Layout.fillWidth: true }
                                    }
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                                }
                                delegate: Column {
                                    id: hotkeyEntry
                                    required property var modelData
                                    required property int index
                                    width: hotkeyList.width
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
                                        readonly property bool macro: modelData.script !== undefined
                                        Accessible.name: (macro ? modelData.script + " " + modelData.macro : modelData.text) + " " + modelData.accel
                                                         + (macro && modelData.note.length > 0 ? ", " + modelData.note : "")
                                        background: Rectangle {
                                            color: hotkeyRow.highlighted ? Theme.select : "transparent"
                                            Rectangle { visible: hotkeyRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                                        }
                                        contentItem: RowLayout {
                                            Item {
                                                visible: hotkeyList.gutter
                                                Layout.preferredWidth: 13
                                                Layout.fillHeight: true
                                                Rectangle {
                                                    visible: hotkeyRow.modelData.inBlock
                                                    x: 6; width: 1; height: parent.height
                                                    color: Theme.text
                                                }
                                            }
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
                                        BlockBox {
                                            objectName: "hotkeyBlock" + hotkeyRow.index
                                            block: hotkeyRow.modelData.block
                                            position: hotkeyRow.index
                                            y: hotkeyRow.height - height / 2
                                            z: 1
                                        }
                                    }
                                    Label {
                                        objectName: "hotkeyClash"
                                        visible: (hotkeyEntry.modelData.clash ?? "").length > 0
                                        text: hotkeyEntry.modelData.clash ?? ""
                                        color: Theme.muted
                                        font.underline: true
                                        leftPadding: 22
                                        width: hotkeyList.width
                                        elide: Text.ElideRight
                                        Accessible.role: Accessible.Link
                                        Accessible.name: text
                                        TapHandler {
                                            cursorShape: Qt.PointingHandCursor
                                            onTapped: {
                                                const other = (hotkeyEntry.modelData.clashRows ?? [])[0]
                                                const at = hotkeyList.model.findIndex(r => r.row === other)
                                                if (at >= 0) {
                                                    dialog.hotkeys.select(at)
                                                    hotkeyList.positionViewAtIndex(at, ListView.Contain)
                                                }
                                            }
                                        }
                                    }
                                }
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
                                IconTextButton {
                                    iconRole: "hotkeys"
                                    id: hotkeyMapButton
                                    objectName: "hotkeyMap"
                                    text: qsTr("Map shortcut…")
                                    enabled: dialog.hotkeys.mapProblem.length === 0
                                    onClicked: dialog.mapHotkey(hotkeyMapButton)
                                }
                                IconTextButton {
                                    iconRole: "restore-default"
                                    objectName: "hotkeyRestore"
                                    text: qsTr("Restore default")
                                    onClicked: dialog.hotkeys.optionsReset()
                                }
                                IconTextButton { objectName: "hotkeyRemove"; iconRole: "delete"; text: qsTr("Delete"); onClicked: dialog.hotkeys.optionsDelete() }
                            }
                        }
                    }
                }

                SettingsPage {
                    page: "ai"
                    SettingsAiPage { id: aiPage; dialog: dialog }
                }

                SettingsPage {
                    page: "updates"
                    objectName: "settingsPageUpdates"
                    Section {
                        key: "updates.checking"
                        ruled: false
                        SettingCheck { setting: "updater.autoCheck" }
                        SettingCheck {
                            setting: "updater.checkForStable"
                            visible: !dialog.updates || dialog.updates.stableOnlyOption
                        }
                        SettingsRow {
                            // MUST KILL: the docs* properties are read by name by the docs export
                            readonly property string docsSetting: "updater.skippedVersion"
                            readonly property string docsControl: "readonly"
                            readonly property string docsLabel: text
                            visible: (dialog.values["updater.skippedVersion"] ?? "").length > 0
                            text: qsTr("Skipped version")
                            Label {
                                objectName: "settingsSkippedVersion"
                                text: dialog.values["updater.skippedVersion"] ?? ""
                            }
                            IconTextButton {
                                iconRole: "check-updates"
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
                    Section {
                        key: "updates.installing"
                        SettingCheck {
                            setting: "updater.autoInstall"
                            visible: !dialog.updates || dialog.updates.installsUpdates
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
                    Section {
                        key: "updates.plugins"
                        StoreCheck { setting: "plugins.checkUpdates" }
                        StoreCheck { setting: "plugins.autoUpdate" }
                    }
                    Section {
                        key: "updates.privacy"
                        SettingCheck { setting: "privacy.usageData" }
                        LinkButton {
                            objectName: "settingsWhatIsSent"
                            iconRole: "help"
                            text: qsTr("What is sent")
                            Layout.leftMargin: 22
                            onClicked: dialog.app.openWhatIsSent()
                        }
                        SettingCheck {
                            setting: "privacy.alwaysSendCrashReports"
                            visible: !dialog.crashReports || dialog.crashReports.canSend
                        }
                        SettingCheck { setting: "plugins.network" }
                    }
                    Section {
                        key: "updates.discord"
                        DiscordSettingsSection { dialog: dialog; scroller: rowsView.contentItem }
                    }
                }

                        }
                        SettingsPage {
                            page: "plugins"
                            objectName: "settingsPagePlugins"
                            visible: dialog.pageShown("plugins")
                            RowLayout {
                                objectName: "settingsNoPluginSettings"
                                visible: !dialog.projectTab && dialog.pluginsWithSettings.length === 0
                                spacing: 8
                                Label {
                                    text: qsTr("No plugin has settings.")
                                    color: Theme.muted
                                }
                                IconTextButton {
                                    iconRole: "plugins"
                                    text: qsTr("Plugins…")
                                    onClicked: dialog.pluginsRequested("")
                                }
                            }
                            Repeater {
                                model: dialog.projectTab ? dialog.projectPlugins : dialog.pluginsWithSettings
                                PluginSection {}
                            }
                        }
                        ColumnLayout {
                            objectName: "settingsShortcutResults"
                            visible: dialog.searching && !dialog.projectTab && dialog.commandMatches.length > 0
                            spacing: 8
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Shortcuts")
                                font.bold: true
                                color: Theme.text
                            }
                            Repeater {
                                model: dialog.commandMatches.slice(0, 5)
                                RowLayout {
                                    required property var modelData
                                    spacing: 8
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; color: Theme.text; Layout.fillWidth: true }
                                    Label { text: modelData.keys.join(", "); color: Theme.muted }
                                }
                            }
                            LinkButton {
                                objectName: "settingsShowAllShortcuts"
                                iconRole: "hotkeys"
                                text: qsTr("Show all %n shortcuts", "", dialog.commandMatches.length)
                                onClicked: dialog.showShortcutsFor(dialog.query)
                            }
                        }
                    }
                }
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
    }

    signal opened()
    signal pluginsRequested(string plugin)
    readonly property var commandMatches: {
        const words = query.split(" ").filter(w => w.length > 0 && !w.startsWith("@"))
        if (words.length === 0 || !hotkeys)
            return []
        return hotkeys.matching(words.join(" "))
    }
    function showShortcutsFor(text) {
        searchField.text = ""
        query = ""
        hotkeySearch.text = text
        showNode("hotkeys")
    }
    function addToken(token) {
        if (searchField.text.split(" ").includes(token))
            return
        searchField.text = (token + " " + searchField.text.trim()).trim() + " "
        searchField.forceActiveFocus()
        searchField.cursorPosition = searchField.text.length
    }
    Shortcut {
        sequences: [StandardKey.Find]
        context: Qt.WindowShortcut
        onActivated: searchField.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    component PluginSection: ColumnLayout {
        id: pluginSection
        required property var modelData
        readonly property string key: "plugins." + modelData.id
        readonly property var groups: {
            const rows = (modelData.settings ?? []).slice().sort((a, b) => (a.order ?? 0) - (b.order ?? 0))
            const out = []
            for (const s of rows) {
                const name = s.group ?? ""
                let g = out.find(g => g.name === name)
                if (!g) {
                    g = { name: name, settings: [] }
                    out.push(g)
                }
                g.settings.push(s)
            }
            return out
        }
        objectName: "pluginSettings_" + modelData.id
        visible: dialog.sectionShown(key)
        spacing: 12
        Layout.fillWidth: true
        Component.onCompleted: dialog.registerSection(key, pluginSection, false)
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            Image {
                visible: (pluginSection.modelData.icon ?? "").length > 0
                source: pluginSection.modelData.icon ?? ""
                sourceSize.width: 32
                sourceSize.height: 32
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
                    text: pluginSection.modelData.name
                    font.bold: true
                    color: Theme.text
                    Accessible.role: Accessible.Heading
                }
                Label {
                    text: [pluginSection.modelData.version, pluginSection.modelData.description].filter(t => (t ?? "").length > 0).join(" · ")
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            IconTextButton {
                objectName: "pluginSettingsManage"
                iconRole: "plugins"
                text: qsTr("Manage plugin…")
                onClicked: dialog.pluginsRequested(pluginSection.modelData.id)
            }
        }
        Label {
            visible: pluginSection.modelData.enabled !== true
            text: qsTr("%1 is turned off. Its settings apply when it’s on.").arg(pluginSection.modelData.name)
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Repeater {
            model: pluginSection.groups
            ColumnLayout {
                id: group
                required property var modelData
                spacing: 12
                Layout.fillWidth: true
                Label {
                    visible: group.modelData.name.length > 0
                    text: group.modelData.name
                    font.bold: true
                    color: Theme.text
                }
                Repeater {
                    model: group.modelData.settings
                    PluginSettingRow { plugin: pluginSection.modelData.id }
                }
            }
        }
    }
    component PluginSettingRow: SettingsRow {
        id: pluginRow
        required property var modelData
        required property string plugin
        property string problem: ""
        readonly property string type: modelData.type
        readonly property bool inProject: dialog.projectTab
        readonly property var value: inProject && modelData.project !== undefined ? modelData.project : modelData.value
        readonly property bool deprecated: (modelData.deprecated ?? "").length > 0
        host: dialog
        setting: "plugin." + plugin + "." + modelData.id
        text: modelData.title ?? modelData.id
        note: deprecated ? modelData.deprecated : (modelData.description ?? "")
        inline: type === "boolean"
        visible: dialog.shows(setting) && (!deprecated || dialog.pluginChanged(setting))
        function keep(value) {
            problem = dialog.keepPluginSetting(plugin, modelData.id, value)
            if (problem.length > 0)
                pluginField.Accessible.announce(problem, Accessible.Assertive)
        }
        Item {
            id: pluginField
            implicitWidth: control.item ? control.item.implicitWidth : 0
            implicitHeight: control.item ? control.item.implicitHeight : 0
            Layout.fillWidth: pluginRow.type === "string" || pluginRow.type === "list" || pluginRow.type === "secret"
            Loader {
                id: control
                width: parent.width
                sourceComponent: pluginRow.type === "boolean" ? boolControl
                               : pluginRow.type === "choice" ? choiceControl
                               : pluginRow.type === "integer" || pluginRow.type === "number" ? numberControl
                               : pluginRow.type === "string" ? (pluginRow.modelData.multiline === true ? textControl : stringControl)
                               : pluginRow.type === "list" ? listControl : secretControl
            }
        }
        Label {
            visible: pluginRow.problem.length > 0
            text: pluginRow.problem
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Component {
            id: boolControl
            CheckBox {
                objectName: "setting_" + pluginRow.setting
                text: pluginRow.text
                leftPadding: 1
                spacing: 9
                checked: pluginRow.value === true
                Accessible.name: text
                Accessible.description: pluginRow.note
                onToggled: pluginRow.keep(checked)
            }
        }
        Component {
            id: choiceControl
            ComboBox {
                objectName: "setting_" + pluginRow.setting
                model: (pluginRow.modelData.options ?? []).map(o => o.title)
                currentIndex: (pluginRow.modelData.options ?? []).findIndex(o => o.value === pluginRow.value)
                Accessible.name: pluginRow.text
                onActivated: index => pluginRow.keep(pluginRow.modelData.options[index].value)
            }
        }
        Component {
            id: numberControl
            TextField {
                objectName: "setting_" + pluginRow.setting
                text: String(pluginRow.value ?? "")
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Accessible.name: pluginRow.text
                onEditingFinished: {
                    const n = Number(text)
                    if (text.trim().length === 0 || isNaN(n) || (pluginRow.type === "integer" && !Number.isInteger(n))) {
                        pluginRow.problem = qsTr("Enter a number.")
                        Accessible.announce(pluginRow.problem, Accessible.Assertive)
                        return
                    }
                    if (n !== pluginRow.value)
                        pluginRow.keep(n)
                }
            }
        }
        Component {
            id: stringControl
            TextField {
                objectName: "setting_" + pluginRow.setting
                text: pluginRow.value ?? ""
                Accessible.name: pluginRow.text
                onEditingFinished: if (text !== pluginRow.value) pluginRow.keep(text)
            }
        }
        Component {
            id: textControl
            TextArea {
                objectName: "setting_" + pluginRow.setting
                text: pluginRow.value ?? ""
                wrapMode: TextEdit.Wrap
                Layout.minimumHeight: 80
                Accessible.name: pluginRow.text
                onActiveFocusChanged: if (!activeFocus && text !== pluginRow.value) pluginRow.keep(text)
            }
        }
        Component {
            id: listControl
            ColumnLayout {
                id: list
                readonly property var items: pluginRow.value ?? []
                readonly property int limit: pluginRow.modelData.maxItems ?? 256
                function without(index) {
                    return items.filter((_, i) => i !== index)
                }
                function replaced(index, text) {
                    return items.map((item, i) => i === index ? text : item)
                }
                objectName: "setting_" + pluginRow.setting
                spacing: 6
                Repeater {
                    model: list.items
                    RowLayout {
                        id: item
                        required property string modelData
                        required property int index
                        spacing: 8
                        Layout.fillWidth: true
                        TextField {
                            objectName: "settingListItem" + item.index
                            text: item.modelData
                            Layout.fillWidth: true
                            Accessible.name: qsTr("%1, item %2").arg(pluginRow.text, item.index + 1)
                            onEditingFinished: if (text !== item.modelData) pluginRow.keep(list.replaced(item.index, text))
                        }
                        IconButton {
                            objectName: "settingListRemove" + item.index
                            iconRole: "remove"
                            text: qsTr("Remove item %1").arg(item.index + 1)
                            onClicked: pluginRow.keep(list.without(item.index))
                        }
                    }
                }
                IconTextButton {
                    objectName: "settingListAdd"
                    iconRole: "add"
                    text: qsTr("Add")
                    enabled: list.items.length < list.limit
                    onClicked: pluginRow.keep(list.items.concat([""]))
                }
            }
        }
        Component {
            id: secretControl
            RowLayout {
                id: secret
                property bool shown: false
                readonly property bool kept: typeof pluginRow.value === "string"
                spacing: 8
                TextField {
                    objectName: "setting_" + pluginRow.setting
                    text: pluginRow.value ?? ""
                    echoMode: secret.shown ? TextInput.Normal : TextInput.Password
                    placeholderText: qsTr("Not set")
                    Layout.fillWidth: true
                    Accessible.name: pluginRow.text
                    onEditingFinished: if (text !== (pluginRow.value ?? "")) pluginRow.keep(text.length > 0 ? text : null)
                }
                IconTextButton {
                    objectName: "settingSecretShow"
                    iconRole: secret.shown ? "hide" : "show"
                    text: secret.shown ? qsTr("Hide") : qsTr("Show")
                    enabled: secret.kept
                    onClicked: secret.shown = !secret.shown
                }
                IconTextButton {
                    objectName: "settingSecretClear"
                    iconRole: "clear"
                    text: qsTr("Clear")
                    enabled: secret.kept
                    onClicked: {
                        secret.shown = false
                        pluginRow.keep(null)
                    }
                }
            }
        }
    }


    FolderChooser {
        id: fontsFolderDialog
        objectName: "fontsFolderDialog"
        title: qsTr("Choose external font folder")
        onAccepted: {
            const path = dialog.app.settingsFolderPath(selectedFolder)
            fontsFolder.text = path
            dialog.put("fonts.externalDirectory", path)
        }
    }

    FileChooser {
        id: legacyHotkeysFile
        objectName: "legacyHotkeysFile"
        title: qsTr("Import legacy shortcuts")
        nameFilters: [qsTr("Shortcuts (Hotkeys.txt)"), qsTr("All files (*)")]
        onAccepted: dialog.importLegacyHotkeys(dialog.app.localPath(selectedFile))
    }
    function importLegacyHotkeys(path) {
        const r = hotkeys.importLegacyScripts(path)
        hotkeys.search(hotkeySearch.text)
        if (!notices)
            return r
        const title = qsTr("Import legacy shortcuts")
        if (r.read !== true) {
            notices.post("error", title, qsTr("The file could not be read."), { details: path })
            return r
        }
        const parts = [r.added > 0 ? qsTr("Added %n script shortcut(s).", "", r.added) : qsTr("No script shortcuts were added.")]
        if (r.kept > 0)
            parts.push(qsTr("%n kept the keys you gave them.", "", r.kept))
        if (r.skipped > 0)
            parts.push(qsTr("%n skipped: their keys are taken.", "", r.skipped))
        notices.post("info", title, parts.join(" "), r.skipped > 0 ? { details: r.skippedNames.join("\n") } : {})
        return r
    }

    QtObject {
        id: themeColourTarget
        property string theme: ""
        property string role: ""
        property var before: []
        function changeColour(c) {
            dialog.put("appearance.colours", Theme.withUserColour(dialog.values["appearance.colours"] ?? [], theme, role,
                                                                  dialog.colourPicker.htmlText(c)))
            return true
        }
        function endDialog(accepted) {
            if (!accepted)
                dialog.put("appearance.colours", before)
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

