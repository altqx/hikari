pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Export"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: block
    objectName: "beforeExport"
    required property var app
    required property var host
    property real videoFps: 0
    property bool exporting: false
    property var lists: ({})
    property var steps: []
    property var saved: []
    property string recipe: ""
    property string recipeTier: ""
    property bool fromFile: false
    property bool expanded: false
    property int settingsOpen: -1
    property var luaDialog: ({})
    property Item luaControls: null
    property var luaValues: ({})
    property var colourPicker: null
    property string progress: ""
    property var outcome: []
    readonly property bool failed: outcome.some(s => s.state === "failed")
    readonly property int missingCount: steps.filter(s => isMissing(s)).length
    readonly property bool changed: (recipe !== "" || fromFile) && JSON.stringify(steps) !== JSON.stringify(saved)
    readonly property string choiceText: fromFile ? qsTr("From this file (Aegisub)") : recipe !== "" ? recipe
                                       : steps.length > 0 ? qsTr("Unsaved steps") : qsTr("No steps")
    readonly property var removeTagsFamilies: [
        qsTranslate("HikariSub.Tags", "Colour"), qsTranslate("HikariSub.Tags", "Alpha"), qsTranslate("HikariSub.Tags", "Border"),
        qsTranslate("HikariSub.Tags", "Shadow"), qsTranslate("HikariSub.Tags", "Blur"), qsTranslate("HikariSub.Tags", "Font"),
        qsTranslate("HikariSub.Tags", "Scale"), qsTranslate("HikariSub.Tags", "Spacing"), qsTranslate("HikariSub.Tags", "Rotation"),
        qsTranslate("HikariSub.Tags", "Shear"), qsTranslate("HikariSub.Tags", "Bold, italic, underline, strikeout"),
        qsTranslate("HikariSub.Tags", "Position"), qsTranslate("HikariSub.Tags", "Origin"), qsTranslate("HikariSub.Tags", "Alignment"),
        qsTranslate("HikariSub.Tags", "Clip"), qsTranslate("HikariSub.Tags", "Fade"), qsTranslate("HikariSub.Tags", "Transforms"),
        qsTranslate("HikariSub.Tags", "Karaoke"), qsTranslate("HikariSub.Tags", "Reset"), qsTranslate("HikariSub.Tags", "Wrap style"),
        qsTranslate("HikariSub.Tags", "Drawing"), qsTranslate("HikariSub.Tags", "Other tags")
    ]
    readonly property var removeTagsCleanup: [
        { key: "removeComments", name: qsTranslate("HikariSub.Tags", "Remove comments") },
        { key: "mergeBlocks", name: qsTranslate("HikariSub.Tags", "Merge adjacent tag blocks") },
        { key: "dropRedundant", name: qsTranslate("HikariSub.Tags", "Drop overridden tags") },
        { key: "cleanTags", name: qsTranslate("HikariSub.Tags", "Clean Tags rules") },
        { key: "deleteEmptyLines", name: qsTranslate("HikariSub.Tags", "Delete lines left empty") }
    ]

    signal exportWithout(int step)

    spacing: 6
    Layout.fillWidth: true

    function clone(v) { return JSON.parse(JSON.stringify(v ?? [])) }
    function stepInfo(id) { return (lists.available ?? []).find(a => a.id === id) }
    function isLua(id) { return id.startsWith("lua:") }
    function isMissing(step) { return isLua(step.id) && !stepInfo(step.id) }
    function scriptOf(id) {
        const info = stepInfo(id)
        if (info)
            return info.script
        const slash = id.indexOf("/")
        return slash > 4 ? id.slice(4, slash) : ""
    }
    function nameOf(step) {
        const info = stepInfo(step.id)
        if (info)
            return info.name
        const slash = step.id.indexOf("/")
        return slash >= 0 ? step.id.slice(slash + 1) : step.id
    }
    function marked(list) {
        return clone(list).map(s => isMissing(s) ? Object.assign(s, { on: false }) : s)
    }
    function load() {
        lists = app.exportSteps()
        chooseNone()
        outcome = []
        progress = ""
    }
    function refreshRecipes() {
        lists = Object.assign({}, lists, { recipes: app.exportSteps().recipes })
    }
    function chooseNone() {
        steps = []
        saved = []
        recipe = ""
        recipeTier = ""
        fromFile = false
        expanded = false
        settingsOpen = -1
    }
    function addSteps() {
        steps = clone(lists.newList)
        saved = []
        recipe = ""
        recipeTier = ""
        fromFile = false
        expanded = true
    }
    function chooseRecipe(name, tier) {
        steps = marked(app.exportRecipe(name, tier))
        saved = clone(steps)
        recipe = name
        recipeTier = tier
        fromFile = false
        expanded = true
        settingsOpen = -1
    }
    function chooseFromFile() {
        steps = marked(lists.fromFile)
        saved = clone(steps)
        recipe = ""
        recipeTier = ""
        fromFile = true
        expanded = true
        settingsOpen = -1
    }
    function luaOpen() {
        return settingsOpen >= 0 && settingsOpen < steps.length && luaControls !== null
            && (luaDialog.controls ?? []).length > 0 && steps[settingsOpen].id === luaDialog.id
    }
    function assign(next) {
        if (luaOpen()) {
            const values = luaControls.values()
            luaValues = { id: luaDialog.id, values: values }
            const open = next.findIndex(s => s.id === luaDialog.id)
            if (open >= 0)
                next[open].settings = app.exportStepSettings(luaDialog.id, values)
        }
        steps = next
        outcome = []
    }
    function edit(index, change) {
        const next = clone(steps)
        change(next[index])
        assign(next)
    }
    function setOn(index, on) { edit(index, s => s.on = on) }
    function setSetting(index, key, value) {
        edit(index, s => { s.settings = Object.assign({}, s.settings ?? {}, { [key]: value }) })
    }
    function resetSettings(index) {
        const fresh = (lists.newList ?? []).find(s => s.id === steps[index].id)
        const next = clone(steps)
        next[index].settings = fresh ? clone(fresh.settings) : {}
        steps = next
        outcome = []
        if (isLua(steps[index].id) && settingsOpen === index)
            askDialog(index)
    }
    function move(from, to) {
        if (from === to || from < 0 || to < 0 || from >= steps.length || to >= steps.length)
            return
        const next = clone(steps)
        next.splice(to, 0, next.splice(from, 1)[0])
        const open = settingsOpen === from ? to : settingsOpen
        assign(next)
        settingsOpen = open
    }
    function keepOpenLuaSettings() {
        if (luaOpen())
            assign(clone(steps))
    }
    function toggleSettings(index) {
        keepOpenLuaSettings()
        settingsOpen = settingsOpen === index ? -1 : index
        if (settingsOpen >= 0 && isLua(steps[index].id))
            askDialog(index)
    }
    function askDialog(index) {
        const id = steps[index].id
        luaValues = ({})
        luaDialog = { id: id, loading: true }
        if (!app.askExportStepDialog(id, steps[index].settings ?? {}))
            luaDialog = { id: id, problem: qsTr("The script is busy or isn’t loaded, so its settings can’t be shown.") }
    }
    function choice() {
        keepOpenLuaSettings()
        return steps.some(s => s.on) ? steps : []
    }
    function saveRecipe(name, tier) {
        keepOpenLuaSettings()
        if (!app.saveExportRecipe(name, tier, steps))
            return
        refreshRecipes()
        recipe = name.trim()
        recipeTier = (lists.hasProject ?? false) ? tier : "mine"
        fromFile = false
        saved = clone(steps)
    }
    function summary(step) {
        const s = step.settings ?? {}
        switch (step.id) {
        case "remove-tags": {
            const parts = []
            const families = String(s.families ?? "").split(",").filter(f => f !== "")
            if (families.length > 0)
                parts.push(qsTr("%n tag kind(s)", "", families.length))
            if (s.cleanTags)
                parts.push(qsTranslate("HikariSub.Tags", "Clean Tags rules"))
            return parts.length > 0 ? parts.join(" · ") : qsTr("Nothing chosen")
        }
        case "transform-frame-rate": {
            const output = s.output === "constant" ? String(s.outputFps ?? "") : qsTr("Variable")
            const text = qsTr("%1 → %2").arg(String(s.input ?? ""), output)
            return s.reverse ? qsTr("%1, reversed").arg(text) : text
        }
        case "karaoke-template":
            return qsTr("Templates in this file")
        case "fix-styles":
            return qsTr("Lines whose style is missing")
        }
        return ""
    }
    function recipeMenuRows() {
        const rows = []
        for (const tier of ["project", "mine"]) {
            const recipes = (lists.recipes ?? []).filter(r => r.tier === tier)
            if (recipes.length === 0)
                continue
            rows.push({ heading: tier === "project" ? qsTr("This project") : qsTr("Yours") })
            for (const r of recipes)
                rows.push(r)
        }
        return rows
    }
    function chipText(step) {
        if (!isLua(step.id))
            return qsTr("Built-in")
        return qsTr("Lua · %1").arg(scriptOf(step.id))
    }
    function stateText(row) {
        switch (row.state) {
        case "done":
            return row.id === "karaoke-template" ? qsTr("%n line(s) made", "", row.lines) : qsTr("%n line(s) changed", "", row.lines)
        case "skipped":
            return qsTr("Skipped: its script isn’t loaded")
        case "notRun":
            return qsTr("Didn’t run")
        }
        switch (row.problem) {
        case "noTimecodes":
            return qsTr("Variable needs a video or timecodes file")
        case "badFrameRate":
            return qsTr("Enter frame rates above 0")
        case "busy":
            return qsTr("A macro is running, so the script couldn’t start")
        case "notApplied":
            return qsTr("Couldn’t apply the script’s changes")
        }
        return row.error !== "" ? row.error : qsTr("Stopped with a script error")
    }
    function ranText(rows) {
        const ran = (rows ?? []).filter(r => r.state === "done").map(r => r.id === "karaoke-template"
            ? qsTr("%1 (%n line(s) made)", "", r.lines).arg(r.name) : r.name)
        return ran.length > 0 ? qsTr("Ran before export: %1.").arg(ran.join(", ")) : ""
    }
    function outcomeIcon(state) {
        const icons = { done: "karaoke-check", failed: "notice-warning" }
        return icons[state] ?? ""
    }
    function failedRow() {
        return outcome.find(s => s.state === "failed")
    }
    function turnOffFailed() {
        const row = failedRow()
        const index = row ? steps.findIndex(s => s.id === row.id && s.on) : -1
        if (index < 0)
            return
        setOn(index, false)
        exportWithout(index)
    }

    Connections {
        target: block.app
        function onExportStepDialogReady(id, dialog) {
            if (id === block.luaDialog.id)
                block.luaDialog = Object.assign({ id: id }, dialog)
        }
        function onExportStepStarted(name, step, count) {
            block.progress = qsTr("Before export · %1 · step %2 of %3").arg(name, step, count)
        }
    }

    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Button {
            id: recipeButton
            objectName: "beforeExportRecipe"
            text: block.choiceText
            Accessible.name: qsTr("Before export: %1").arg(block.choiceText)
            onClicked: recipeMenu.popup(recipeButton, 0, recipeButton.height)
        }
        TierChip {
            objectName: "beforeExportTier"
            visible: block.recipe !== "" && (block.lists.hasProject ?? false)
            tier: block.recipeTier
        }
        Label {
            objectName: "beforeExportChanged"
            visible: block.changed
            text: qsTr("changed")
            color: Theme.muted
        }
        Item { Layout.fillWidth: true }
        Button {
            objectName: "beforeExportShowSteps"
            flat: true
            text: block.steps.length === 0 ? qsTr("Add steps") : block.expanded ? qsTr("Hide steps") : qsTr("Show steps")
            onClicked: {
                if (block.steps.length === 0)
                    block.addSteps()
                else
                    block.expanded = !block.expanded
            }
        }
    }
    Label {
        objectName: "beforeExportNone"
        visible: block.steps.length === 0
        text: qsTr("Writes the document as it is.")
        color: Theme.muted
    }

    ShellMenu {
        id: recipeMenu
        objectName: "beforeExportRecipeMenu"
        ShellMenuItem {
            objectName: "beforeExportNoSteps"
            iconRole: "clear"
            text: qsTr("No steps")
            onTriggered: block.chooseNone()
        }
        MenuSeparator {}
        Instantiator {
            model: block.recipeMenuRows()
            delegate: ShellMenuItem {
                required property var modelData
                objectName: modelData.heading ? "" : "beforeExportRecipe_" + modelData.tier + "_" + modelData.name
                text: modelData.heading ? modelData.heading : modelData.name
                iconRole: modelData.heading ? "" : modelData.tier
                enabled: !modelData.heading
                font.strikeout: modelData.hidden === true
                description: modelData.hidden === true ? qsTr("Hidden by the project’s recipe of the same name") : ""
                onTriggered: block.chooseRecipe(modelData.name, modelData.tier)
            }
            onObjectAdded: (index, object) => recipeMenu.insertItem(2 + index, object)
            onObjectRemoved: (index, object) => recipeMenu.removeItem(object)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "beforeExportFromFile"
            iconRole: "import"
            text: qsTr("From this file (Aegisub)")
            enabled: (block.lists.fromFile ?? []).length > 0
            onTriggered: block.chooseFromFile()
        }
        ShellMenuItem {
            objectName: "beforeExportSaveRecipe"
            iconRole: "save-as"
            text: qsTr("Save recipe…")
            enabled: block.steps.length > 0
            onTriggered: savePopover.openAt(recipeButton)
        }
        ShellMenuItem {
            objectName: "beforeExportManageRecipes"
            iconRole: "edit"
            text: qsTr("Manage recipes…")
            enabled: (block.lists.recipes ?? []).length > 0
            onTriggered: managePopover.openAt(recipeButton)
        }
    }

    Rectangle {
        objectName: "beforeExportSteps"
        visible: block.expanded && block.steps.length > 0
        Layout.fillWidth: true
        implicitHeight: stepsColumn.implicitHeight + 2
        radius: 6
        color: "transparent"
        border.color: Theme.line
        ColumnLayout {
            id: stepsColumn
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: header.implicitHeight + 12
                color: Theme.raised
                radius: 6
                RowLayout {
                    id: header
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    Label {
                        text: qsTr("Run in this order on a copy; the document doesn’t change.")
                        color: Theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    Button {
                        objectName: "beforeExportSaveRecipeButton"
                        flat: true
                        text: qsTr("Save recipe…")
                        onClicked: savePopover.openAt(this)
                    }
                }
            }
            Repeater {
                id: stepRows
                model: block.steps
                delegate: ColumnLayout {
                    id: stepRow
                    required property var modelData
                    required property int index
                    readonly property bool missing: block.isMissing(modelData)
                    readonly property bool open: block.settingsOpen === index
                    objectName: "beforeExportStep_" + modelData.id
                    spacing: 0
                    Layout.fillWidth: true
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: rowLayout.implicitHeight + 8
                        color: stepRow.open ? Theme.select : dropArea.containsDrag ? Theme.raised : "transparent"
                        DropArea {
                            id: dropArea
                            anchors.fill: parent
                            keys: ["beforeExportStep"]
                            onDropped: drop => block.move(drop.source.stepIndex, stepRow.index)
                        }
                        RowLayout {
                            id: rowLayout
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 6
                            Icon {
                                id: grip
                                objectName: "beforeExportGrip"
                                readonly property int stepIndex: stepRow.index
                                iconRole: "vector-drag"
                                size: 16
                                color: Theme.muted
                                Drag.active: gripDrag.active
                                Drag.keys: ["beforeExportStep"]
                                Drag.source: grip
                                Drag.hotSpot.y: height / 2
                                DragHandler {
                                    id: gripDrag
                                    yAxis.enabled: true
                                    xAxis.enabled: false
                                    onActiveChanged: if (!active) { grip.Drag.drop(); grip.x = 0; grip.y = 0 }
                                }
                            }
                            CheckBox {
                                objectName: "beforeExportOn"
                                checked: stepRow.modelData.on
                                enabled: !stepRow.missing
                                Accessible.name: qsTr("Run %1").arg(block.nameOf(stepRow.modelData))
                                onToggled: block.setOn(stepRow.index, checked)
                                Keys.onPressed: event => {
                                    if (event.modifiers & Qt.AltModifier && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                        block.move(stepRow.index, stepRow.index + (event.key === Qt.Key_Up ? -1 : 1))
                                        event.accepted = true
                                    }
                                }
                            }
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label {
                                    text: block.nameOf(stepRow.modelData)
                                    font.weight: stepRow.modelData.on ? Font.DemiBold : Font.Normal
                                    color: stepRow.modelData.on ? Theme.text : Theme.muted
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    objectName: "beforeExportSummary"
                                    text: stepRow.missing ? qsTr("%1 isn’t loaded · Export skips it").arg(block.scriptOf(stepRow.modelData.id))
                                                          : block.summary(stepRow.modelData)
                                    visible: text !== ""
                                    color: stepRow.missing ? Theme.warning : Theme.muted
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                            Label {
                                text: block.chipText(stepRow.modelData)
                                color: Theme.muted
                                font: block.host.fontAt(11)
                            }
                            IconButton {
                                objectName: "beforeExportSettings"
                                iconRole: "settings"
                                text: qsTr("Settings for %1").arg(block.nameOf(stepRow.modelData))
                                visible: ["remove-tags", "transform-frame-rate"].includes(stepRow.modelData.id)
                                         || (block.stepInfo(stepRow.modelData.id)?.hasConfig ?? false)
                                checkable: true
                                checked: stepRow.open
                                onClicked: block.toggleSettings(stepRow.index)
                            }
                            IconButton {
                                id: moreButton
                                objectName: "beforeExportMore"
                                iconRole: "menu-more"
                                text: qsTr("More for %1").arg(block.nameOf(stepRow.modelData))
                                onClicked: stepMenu.popup(moreButton, 0, moreButton.height)
                            }
                            ShellMenu {
                                id: stepMenu
                                ShellMenuItem {
                                    iconRole: "move-up"
                                    text: qsTr("Move up")
                                    enabled: stepRow.index > 0
                                    onTriggered: block.move(stepRow.index, stepRow.index - 1)
                                }
                                ShellMenuItem {
                                    iconRole: "move-down"
                                    text: qsTr("Move down")
                                    enabled: stepRow.index < block.steps.length - 1
                                    onTriggered: block.move(stepRow.index, stepRow.index + 1)
                                }
                                ShellMenuItem {
                                    iconRole: "move-to-top"
                                    text: qsTr("Move to top")
                                    enabled: stepRow.index > 0
                                    onTriggered: block.move(stepRow.index, 0)
                                }
                                MenuSeparator {}
                                ShellMenuItem {
                                    iconRole: "restore-default"
                                    text: qsTr("Reset settings")
                                    onTriggered: block.resetSettings(stepRow.index)
                                }
                            }
                        }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.leftMargin: 28
                        Layout.rightMargin: 8
                        Layout.bottomMargin: 8
                        active: stepRow.open
                        visible: active
                        sourceComponent: stepRow.modelData.id === "remove-tags" ? removeTagsSettings
                                       : stepRow.modelData.id === "transform-frame-rate" ? transformSettings : luaSettings
                        property int stepIndex: stepRow.index
                        property var settings: stepRow.modelData.settings ?? {}
                    }
                }
            }
        }
    }
    Label {
        objectName: "beforeExportMissing"
        visible: block.missingCount > 0
        text: qsTr("%n step(s) won’t run: its script isn’t loaded.", "", block.missingCount)
        color: Theme.warning
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label {
        objectName: "beforeExportProgress"
        visible: block.exporting && block.progress !== ""
        text: block.progress
        color: Theme.muted
    }

    Rectangle {
        objectName: "beforeExportFailure"
        visible: block.failed
        Layout.fillWidth: true
        implicitHeight: failureColumn.implicitHeight + 2
        radius: 6
        color: "transparent"
        border.color: Theme.line
        ColumnLayout {
            id: failureColumn
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: failureHeader.implicitHeight + 12
                color: Theme.raised
                radius: 6
                RowLayout {
                    id: failureHeader
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    Icon { iconRole: "notice-error"; size: 16; color: Theme.danger }
                    Label {
                        objectName: "beforeExportFailureTitle"
                        text: block.failed ? qsTr("Not saved. “%1” failed.").arg(block.failedRow().name) : ""
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    Button {
                        objectName: "beforeExportTurnOff"
                        flat: true
                        text: qsTr("Turn it off and export")
                        onClicked: block.turnOffFailed()
                    }
                    Button {
                        objectName: "beforeExportDetails"
                        flat: true
                        checkable: true
                        visible: block.failed && block.failedRow().error !== ""
                        text: qsTr("Details")
                    }
                }
            }
            Repeater {
                model: block.outcome
                delegate: RowLayout {
                    required property var modelData
                    spacing: 6
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 4
                    Icon {
                        iconRole: block.outcomeIcon(parent.modelData.state)
                        size: 14
                        color: parent.modelData.state === "done" ? Theme.success : Theme.danger
                        visible: iconRole !== ""
                    }
                    Label { text: parent.modelData.name; Layout.preferredWidth: 180; elide: Text.ElideRight }
                    Label {
                        text: block.stateText(parent.modelData)
                        color: parent.modelData.state === "failed" ? Theme.danger : Theme.muted
                        font.family: parent.modelData.state === "failed" && parent.modelData.error !== "" ? "monospace" : font.family
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }
            }
            Label {
                Layout.leftMargin: 10
                Layout.topMargin: 4
                text: qsTr("Write the file: didn’t run")
                color: Theme.muted
            }
            Label {
                Layout.leftMargin: 10
                Layout.topMargin: 6
                Layout.bottomMargin: 8
                text: qsTr("Nothing was written, and the document is unchanged.")
                color: Theme.muted
            }
        }
    }

    Component {
        id: removeTagsSettings
        ColumnLayout {
            id: tagsBox
            readonly property int stepIndex: parent ? parent.stepIndex : -1
            readonly property var settings: parent ? parent.settings : ({})
            readonly property var families: String(settings.families ?? "").split(",").filter(f => f !== "").map(Number)
            objectName: "beforeExportRemoveTagsSettings"
            spacing: 4
            RowLayout {
                Label { text: qsTranslate("HikariSub.Tags", "Blocks") }
                ComboBox {
                    objectName: "beforeExportRemoveTagsScope"
                    model: [qsTranslate("HikariSub.Tags", "All blocks"), qsTranslate("HikariSub.Tags", "Start block"),
                            qsTranslate("HikariSub.Tags", "Inline blocks")]
                    currentIndex: Math.max(0, ["all", "start", "inline"].indexOf(tagsBox.settings.scope ?? "all"))
                    Accessible.name: qsTranslate("HikariSub.Tags", "Blocks")
                    onActivated: index => block.setSetting(tagsBox.stepIndex, "scope", ["all", "start", "inline"][index])
                }
            }
            GridLayout {
                columns: 3
                columnSpacing: 12
                rowSpacing: 0
                Repeater {
                    model: block.removeTagsFamilies
                    delegate: CheckBox {
                        required property string modelData
                        required property int index
                        objectName: "beforeExportFamily" + index
                        text: modelData
                        checked: tagsBox.families.includes(index)
                        onToggled: {
                            const next = tagsBox.families.filter(f => f !== index)
                            if (checked)
                                next.push(index)
                            next.sort((a, b) => a - b)
                            block.setSetting(tagsBox.stepIndex, "families", next.join(","))
                        }
                    }
                }
            }
            Repeater {
                model: block.removeTagsCleanup
                delegate: CheckBox {
                    required property var modelData
                    objectName: "beforeExportCleanup_" + modelData.key
                    text: modelData.name
                    checked: tagsBox.settings[modelData.key] === true
                    onToggled: block.setSetting(tagsBox.stepIndex, modelData.key, checked)
                }
            }
        }
    }

    Component {
        id: transformSettings
        ColumnLayout {
            id: transformBox
            readonly property int stepIndex: parent ? parent.stepIndex : -1
            readonly property var settings: parent ? parent.settings : ({})
            objectName: "beforeExportTransformSettings"
            spacing: 4
            function number(text) { return Number(String(text).replace(",", ".")) }
            function shown(value) { return Number(value ?? 0).toFixed(3).replace(/\.?0+$/, "") }
            RowLayout {
                Label { text: qsTr("Input frame rate") }
                TextField {
                    id: inputFps
                    objectName: "beforeExportInputFps"
                    Layout.preferredWidth: 80
                    text: transformBox.shown(transformBox.settings.input)
                    validator: RegularExpressionValidator { regularExpression: /^\d{0,4}([.,]\d{0,6})?$/ }
                    Accessible.name: qsTr("Input frame rate")
                    onEditingFinished: block.setSetting(transformBox.stepIndex, "input", transformBox.number(text))
                }
                IconTextButton {
                    objectName: "beforeExportInputFromVideo"
                    iconRole: "fps-from-video"
                    visible: block.videoFps > 0
                    text: qsTr("From video")
                    onClicked: block.setSetting(transformBox.stepIndex, "input", block.videoFps)
                }
            }
            RowLayout {
                Label { text: qsTr("Output") }
                RadioButton {
                    objectName: "beforeExportOutputVariable"
                    text: qsTr("Variable (the loaded timecodes)")
                    checked: (transformBox.settings.output ?? "variable") !== "constant"
                    onToggled: if (checked) block.setSetting(transformBox.stepIndex, "output", "variable")
                }
                RadioButton {
                    id: constant
                    objectName: "beforeExportOutputConstant"
                    text: qsTr("Constant")
                    checked: transformBox.settings.output === "constant"
                    onToggled: if (checked) block.setSetting(transformBox.stepIndex, "output", "constant")
                }
                TextField {
                    objectName: "beforeExportOutputFps"
                    enabled: constant.checked
                    Layout.preferredWidth: 80
                    text: transformBox.shown(transformBox.settings.outputFps)
                    validator: RegularExpressionValidator { regularExpression: /^\d{0,4}([.,]\d{0,6})?$/ }
                    Accessible.name: qsTr("Output frame rate")
                    onEditingFinished: block.setSetting(transformBox.stepIndex, "outputFps", transformBox.number(text))
                }
            }
            CheckBox {
                objectName: "beforeExportReverse"
                text: qsTr("Reverse the transformation")
                checked: transformBox.settings.reverse === true
                onToggled: block.setSetting(transformBox.stepIndex, "reverse", checked)
            }
        }
    }

    Component {
        id: luaSettings
        ColumnLayout {
            id: luaBox
            readonly property int stepIndex: parent ? parent.stepIndex : -1
            readonly property var dialog: block.luaDialog
            objectName: "beforeExportLuaSettings"
            spacing: 4
            Label {
                text: qsTr("Settings from %1").arg(block.scriptOf(block.steps[luaBox.stepIndex]?.id ?? ""))
                color: Theme.muted
            }
            Label {
                visible: luaBox.dialog.loading === true
                text: qsTr("Asking the script…")
                color: Theme.muted
            }
            FieldError {
                visible: (luaBox.dialog.problem ?? "") !== ""
                text: luaBox.dialog.problem ?? ""
                Layout.fillWidth: true
            }
            ScriptDialogControls {
                id: luaGrid
                objectName: "beforeExportLuaControls"
                visible: (luaBox.dialog.controls ?? []).length > 0
                controls: luaBox.dialog.controls ?? []
                columns: luaBox.dialog.columns ?? 1
                colourPicker: block.colourPicker
                shortcutsEnabled: false
                Layout.fillWidth: true
                Component.onCompleted: {
                    if (block.luaValues.id === luaBox.dialog.id)
                        fill(block.luaValues.values)
                    block.luaControls = luaGrid
                }
                Component.onDestruction: if (block.luaControls === luaGrid) block.luaControls = null
            }
        }
    }

    Popover {
        id: savePopover
        objectName: "beforeExportSavePopover"
        title: qsTr("Save recipe")
        showTitle: true
        initialFocus: recipeName
        readonly property string name: recipeName.text.trim()
        readonly property bool taken: (block.lists.recipes ?? []).some(r => r.name === name && r.tier === recipeSaveTier.tier)
        onKept: if (name !== "") block.saveRecipe(name, recipeSaveTier.tier)
        TextField {
            id: recipeName
            objectName: "beforeExportRecipeName"
            Layout.preferredWidth: 240
            placeholderText: qsTr("Recipe name")
            text: block.recipe
            Accessible.name: qsTr("Recipe name")
            onAccepted: if (savePopover.name !== "") savePopover.keep()
        }
        Label {
            visible: savePopover.taken
            text: qsTr("A recipe with this name exists; saving replaces it.")
            color: Theme.warning
            wrapMode: Text.Wrap
            Layout.preferredWidth: 240
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            TierSaveButton {
                id: recipeSaveTier
                objectName: "beforeExportRecipeTier"
                listId: "exportRecipes"
                settings: block.app.settings
                hasProject: block.lists.hasProject ?? false
                plainText: savePopover.taken ? qsTr("Replace") : qsTr("Save")
                faceEnabled: savePopover.name !== ""
                onSave: tier => { block.saveRecipe(savePopover.name, tier); savePopover.revert() }
            }
            Button {
                text: qsTr("Cancel")
                onClicked: savePopover.revert()
            }
        }
    }

    Popover {
        id: managePopover
        objectName: "beforeExportManagePopover"
        title: qsTr("Manage recipes")
        showTitle: true
        Repeater {
            model: block.lists.recipes ?? []
            delegate: RowLayout {
                id: recipeRow
                required property var modelData
                objectName: "beforeExportManage_" + modelData.tier + "_" + modelData.name
                spacing: 6
                TextField {
                    objectName: "beforeExportManageName"
                    text: recipeRow.modelData.name
                    font.strikeout: recipeRow.modelData.hidden
                    Layout.preferredWidth: 200
                    Accessible.name: qsTr("Name of %1").arg(recipeRow.modelData.name)
                    onEditingFinished: {
                        const to = text.trim()
                        if (to !== "" && to !== recipeRow.modelData.name
                                && block.app.renameExportRecipe(recipeRow.modelData.tier, recipeRow.modelData.name, to)) {
                            if (block.recipe === recipeRow.modelData.name && block.recipeTier === recipeRow.modelData.tier)
                                block.recipe = to
                            block.refreshRecipes()
                        }
                    }
                }
                TierChip {
                    visible: block.lists.hasProject ?? false
                    tier: recipeRow.modelData.tier
                }
                IconButton {
                    objectName: "beforeExportManageDelete"
                    iconRole: "delete"
                    text: qsTr("Delete %1").arg(recipeRow.modelData.name)
                    onClicked: {
                        if (!block.app.removeExportRecipe(recipeRow.modelData.name, recipeRow.modelData.tier))
                            return
                        if (block.recipe === recipeRow.modelData.name && block.recipeTier === recipeRow.modelData.tier) {
                            block.recipe = ""
                            block.recipeTier = ""
                        }
                        block.refreshRecipes()
                    }
                }
            }
        }
    }
}
