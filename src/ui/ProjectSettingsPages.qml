pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Project"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: project
    objectName: "projectSettings"
    required property var app
    required property int shellWindow
    property var dictionaries: []
    property var values: ({})
    property var yours: ({})
    property var sources: ({})
    property var edited: ({})
    property var cleared: []
    property string problem: ""
    signal reloaded()
    spacing: 0

    readonly property var pages: [
        {id: "spelling", name: qsTranslate("HikariSub.Options", "Spelling and grammar"), iconRole: "spellchecker"},
        {id: "subtitleProperties", name: qsTranslate("HikariSub.Options", "Script properties"), iconRole: "script-properties"},
        {id: "translation", name: qsTranslate("HikariSub.Translation", "Translation"), iconRole: "translation-mode"},
        {id: "ai", name: qsTranslate("HikariSub.Options", "AI"), iconRole: "ai-assistant"}
    ]
    readonly property var scriptFields: [
        {id: "title", name: qsTranslate("HikariSub.ScriptProperties", "Title")},
        {id: "script", name: qsTranslate("HikariSub.ScriptProperties", "Author")},
        {id: "translation", name: qsTranslate("HikariSub.ScriptProperties", "Translator")},
        {id: "editing", name: qsTranslate("HikariSub.ScriptProperties", "Proofreading")},
        {id: "timing", name: qsTranslate("HikariSub.ScriptProperties", "Timer")},
        {id: "update", name: qsTranslate("HikariSub.ScriptProperties", "Editing")}
    ]

    function load() {
        const r = app.openProjectSettings(shellWindow)
        values = r.values ?? {}
        yours = r.yours ?? {}
        sources = r.sources ?? {}
        edited = {}
        cleared = []
        aiKeys.load()
        reloaded()
    }
    function put(id, value) {
        values = Object.assign({}, values, {[id]: value})
        edited = Object.assign({}, edited, {[id]: true})
        cleared = cleared.filter(c => c !== id)
    }
    function showPage(id) {
        const i = pages.findIndex(p => p.id === id)
        if (i >= 0)
            pageList.currentIndex = i
    }
    function useYours(id) {
        values = Object.assign({}, values, {[id]: yours[id]})
        const e = Object.assign({}, edited)
        delete e[id]
        edited = e
        if (!cleared.includes(id))
            cleared = cleared.concat([id])
        reloaded()
    }
    function stateOf(id) {
        if (sources[id] === "local")
            return "local"
        if (edited[id] === true || (sources[id] === "project" && !cleared.includes(id)))
            return "project"
        return "yours"
    }
    function apply() {
        const changed = {}
        for (const id in edited)
            changed[id] = values[id]
        const keysOk = aiKeys.apply()
        const ok = app.applyProjectSettings(shellWindow, changed, cleared) && keysOk
        problem = ok ? "" : qsTr("project.toml could not be written, so some Project settings were not saved.")
        load()
        return ok
    }

    component RowState: RowLayout {
        id: rowState
        required property string key
        property string pair: ""
        readonly property var states: [project.stateOf(key)].concat(pair.length > 0 ? [project.stateOf(pair)] : [])
        readonly property string origin: states.includes("local") ? "local" : states.includes("project") ? "project" : "yours"
        spacing: 4
        Label {
            objectName: "projectState_" + rowState.key
            color: Theme.muted
            text: rowState.origin === "local" ? qsTr("Set in project.local.toml")
                : rowState.origin === "project" ? qsTr("Set in this project") : qsTr("Uses yours")
        }
        IconToolButton {
            objectName: "projectUseYours_" + rowState.key
            visible: rowState.origin === "project"
            iconRole: "restore-default"
            text: qsTr("Use yours")
            onClicked: {
                project.useYours(rowState.key)
                if (rowState.pair.length > 0)
                    project.useYours(rowState.pair)
            }
        }
    }
    component ProjectCheck: RowLayout {
        id: checkRow
        required property string key
        property string text
        Layout.fillWidth: true
        CheckBox {
            id: check
            objectName: "project_" + checkRow.key
            text: checkRow.text
            enabled: project.stateOf(checkRow.key) !== "local"
            leftPadding: 1
            spacing: 9
            Layout.fillWidth: true
            Accessible.name: text
            function refresh() { checked = project.values[checkRow.key] === true }
            Component.onCompleted: refresh()
            Connections { target: project; function onReloaded() { check.refresh() } }
            onToggled: project.put(checkRow.key, checked)
        }
        RowState { key: checkRow.key }
    }
    component ProjectChoice: RowLayout {
        id: choiceRow
        required property string key
        property string text
        property var model: []
        Layout.fillWidth: true
        Label { text: choiceRow.text; Layout.preferredWidth: 160 }
        ComboBox {
            id: choice
            objectName: "project_" + choiceRow.key
            model: choiceRow.model
            enabled: count > 0 && project.stateOf(choiceRow.key) !== "local"
            Layout.preferredWidth: 220
            Accessible.name: choiceRow.text
            function refresh() {
                const v = project.values[choiceRow.key] ?? -1
                currentIndex = v >= 0 && v < count ? v : -1
            }
            Component.onCompleted: refresh()
            onCountChanged: refresh()
            Connections { target: project; function onReloaded() { choice.refresh() } }
            onActivated: index => project.put(choiceRow.key, index)
        }
        Item { Layout.fillWidth: true }
        RowState { key: choiceRow.key }
    }
    component ProjectText: RowLayout {
        id: textRow
        required property string key
        required property string switchSetting
        property string text
        Layout.fillWidth: true
        CheckBox {
            id: on
            objectName: "project_" + textRow.switchSetting
            text: textRow.text
            enabled: project.stateOf(textRow.switchSetting) !== "local"
            leftPadding: 1
            spacing: 9
            Layout.preferredWidth: 160
            Accessible.name: text
            function refresh() { checked = project.values[textRow.switchSetting] === true }
            Component.onCompleted: refresh()
            Connections { target: project; function onReloaded() { on.refresh() } }
            onToggled: project.put(textRow.switchSetting, checked)
        }
        TextField {
            id: field
            objectName: "project_" + textRow.key
            enabled: project.stateOf(textRow.key) !== "local"
            Layout.fillWidth: true
            Accessible.name: textRow.text
            function refresh() { text = project.values[textRow.key] ?? "" }
            Component.onCompleted: refresh()
            Connections { target: project; function onReloaded() { field.refresh() } }
            onTextEdited: project.put(textRow.key, text)
        }
        RowState { key: textRow.key; pair: textRow.switchSetting }
    }

    Rectangle {
        color: Theme.background
        Layout.preferredWidth: 220
        Layout.fillHeight: true
        ListView {
            id: pageList
            objectName: "projectSettingsPages"
            anchors.fill: parent
            anchors.rightMargin: 1
            topMargin: 8
            clip: true
            model: project.pages
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.PageTabList
            delegate: ItemDelegate {
                id: pageItem
                required property var modelData
                required property int index
                objectName: "projectSettingsPage_" + modelData.id
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
                    Icon { iconRole: pageItem.modelData.iconRole }
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
    ColumnLayout {
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        InlineBar {
            objectName: "projectSettingsProblem"
            visible: project.problem.length > 0
            severity: "error"
            text: project.problem
            Layout.fillWidth: true
            onClosed: project.problem = ""
        }
        StackLayout {
            objectName: "projectSettingsStack"
            currentIndex: pageList.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    x: 24
                    y: 20
                    width: parent.width - 48
                    spacing: 10
                    ProjectCheck { key: "editor.spellchecker"; text: qsTranslate("HikariSub.Options", "Check spelling") }
                    ProjectChoice {
                        key: "editor.dictionaryLanguage"
                        text: qsTranslate("HikariSub.Options", "Primary language")
                        model: project.dictionaries
                    }
                    ProjectChoice {
                        key: "grammar.language"
                        text: qsTranslate("HikariSub.Options", "Grammar check")
                        model: [qsTranslate("HikariSub.Options", "Off"), qsTranslate("HikariSub.Options", "English (US)"),
                                qsTranslate("HikariSub.Options", "English (UK)"), qsTranslate("HikariSub.Options", "English (Canada)"),
                                qsTranslate("HikariSub.Options", "English (Australia)")]
                    }
                    Repeater {
                        model: project.app.grammarGroups()
                        ProjectCheck {
                            required property var modelData
                            key: "grammar.group." + modelData.id
                            text: modelData.name
                        }
                    }
                }
            }
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    x: 24
                    y: 20
                    width: parent.width - 48
                    spacing: 10
                    Label {
                        text: qsTr("Saving a file writes the switched-on fields into its Script Info.")
                        color: Theme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: project.scriptFields
                        ProjectText {
                            required property var modelData
                            key: "scriptProperties." + modelData.id
                            switchSetting: "scriptProperties." + modelData.id + "On"
                            text: modelData.name
                        }
                    }
                    ProjectCheck { key: "scriptProperties.askForChange"; text: qsTr("Ask before changing them on save") }
                }
            }
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    x: 24
                    y: 20
                    width: parent.width - 48
                    spacing: 10
                    ProjectCheck {
                        key: "translation.autoMoveTagsFromOriginal"
                        text: qsTranslate("HikariSub.Translation", "Tags from original")
                    }
                }
            }
            ProjectAiKeysPage {
                id: aiKeys
                app: project.app
                shellWindow: project.shellWindow
            }
        }
    }
}
