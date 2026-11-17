pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Project"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: pages
    objectName: "projectSettings"
    required property var dialog
    readonly property var app: dialog.app
    readonly property int shellWindow: dialog.shellWindow
    spacing: 24

    readonly property var scriptFields: [
        {id: "title", name: qsTranslate("HikariSub.ScriptProperties", "Title")},
        {id: "script", name: qsTranslate("HikariSub.ScriptProperties", "Author")},
        {id: "translation", name: qsTranslate("HikariSub.ScriptProperties", "Translator")},
        {id: "editing", name: qsTranslate("HikariSub.ScriptProperties", "Proofreading")},
        {id: "timing", name: qsTranslate("HikariSub.ScriptProperties", "Timer")},
        {id: "update", name: qsTranslate("HikariSub.ScriptProperties", "Editing")}
    ]

    readonly property var exportFormats: [
        {id: "", text: qsTr("Not set")},
        {id: "srt", text: "SubRip (SRT)"},
        {id: "vtt", text: "WebVTT"},
        {id: "sbv", text: "YouTube SBV"},
        {id: "txt", text: qsTranslate("HikariSub.Export", "Plain text")},
        {id: "mdvd", text: "MicroDVD"},
        {id: "mpl2", text: "MPL2"},
        {id: "tmp", text: "TMPlayer"},
        {id: "ttml", text: "TTML (IMSC1)"},
        {id: "dfxp", text: "DFXP"},
        {id: "srv3", text: qsTranslate("HikariSub.Export", "YouTube timed text (srv3)")}
    ]

    function load() { aiKeys.load() }

    component Page: ColumnLayout {
        id: page
        required property string page
        default property alias content: pageColumn.data
        visible: pages.dialog.pageShown(page.page)
        spacing: 12
        Layout.fillWidth: true
        Label {
            text: pages.dialog.catalogue.find(p => p.id === page.page)?.name ?? ""
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
        host: pages.dialog
        project: true
    }
    component ProjectCheck: SettingsRow {
        id: checkRow
        host: pages.dialog
        project: true
        inline: true
        CheckBox {
            id: check
            objectName: "project_" + checkRow.setting
            text: checkRow.text
            leftPadding: 1
            spacing: 9
            Accessible.name: text
            Accessible.description: checkRow.note
            function refresh() { checked = pages.dialog.projectValues[checkRow.setting] === true }
            Component.onCompleted: refresh()
            Connections { target: pages.dialog; function onProjectValuesChanged() { check.refresh() } }
            onToggled: pages.dialog.projectPut(checkRow.setting, checked)
        }
    }
    component ProjectChoice: SettingsRow {
        id: choiceRow
        property var model: []
        host: pages.dialog
        project: true
        ComboBox {
            id: choice
            objectName: "project_" + choiceRow.setting
            model: choiceRow.model
            enabled: count > 0
            Layout.preferredWidth: 260
            Accessible.name: choiceRow.text
            function refresh() {
                const v = pages.dialog.projectValues[choiceRow.setting] ?? -1
                currentIndex = v >= 0 && v < count ? v : -1
            }
            Component.onCompleted: refresh()
            onCountChanged: refresh()
            Connections { target: pages.dialog; function onProjectValuesChanged() { choice.refresh() } }
            onActivated: index => pages.dialog.projectPut(choiceRow.setting, index)
        }
    }
    component ProjectField: SettingsRow {
        id: fieldRow
        property string placeholder
        host: pages.dialog
        project: true
        TextField {
            id: plain
            objectName: "project_" + fieldRow.setting
            placeholderText: fieldRow.placeholder
            Layout.fillWidth: true
            Accessible.name: fieldRow.text
            function refresh() { text = pages.dialog.projectValues[fieldRow.setting] ?? "" }
            function commit() {
                typing.stop()
                if ((pages.dialog.projectValues[fieldRow.setting] ?? "") !== text.trim())
                    pages.dialog.projectPut(fieldRow.setting, text.trim())
            }
            Component.onCompleted: refresh()
            Connections {
                target: pages.dialog
                function onProjectValuesChanged() { if (!plain.activeFocus) plain.refresh() }
            }
            onTextEdited: typing.restart()
            onEditingFinished: commit()
            Timer {
                id: typing
                interval: 500
                onTriggered: plain.commit()
            }
        }
    }
    component ProjectValue: SettingsRow {
        id: valueRow
        host: pages.dialog
        project: true
        TextField {
            objectName: "project_" + valueRow.setting
            readOnly: true
            selectByMouse: true
            text: {
                const v = pages.dialog.projectValues[valueRow.setting]
                return Array.isArray(v) ? v.join(", ") : String(v ?? "")
            }
            Layout.fillWidth: true
            Accessible.name: valueRow.text
        }
        IconTextButton {
            iconRole: "folder-open"
            text: qsTranslate("HikariSub.Options", "Open settings file")
            onClicked: pages.dialog.openSettingsFile(valueRow.setting)
        }
    }
    component ProjectText: SettingsRow {
        id: textRow
        required property var modelData
        readonly property string key: "scriptProperties." + modelData.id
        host: pages.dialog
        project: true
        setting: key
        settings: [key, key + "On"]
        text: modelData.name
        note: ""
        CheckBox {
            id: on
            objectName: "project_" + textRow.key + "On"
            text: qsTranslate("HikariSub.Options", "Write")
            Accessible.name: qsTranslate("HikariSub.Options", "Write %1").arg(textRow.modelData.name)
            function refresh() { checked = pages.dialog.projectValues[textRow.key + "On"] === true }
            Component.onCompleted: refresh()
            Connections { target: pages.dialog; function onProjectValuesChanged() { on.refresh() } }
            onToggled: pages.dialog.projectPut(textRow.key + "On", checked)
        }
        TextField {
            id: field
            objectName: "project_" + textRow.key
            Layout.fillWidth: true
            Accessible.name: textRow.modelData.name
            function refresh() { text = pages.dialog.projectValues[textRow.key] ?? "" }
            function commit() {
                typing.stop()
                if ((pages.dialog.projectValues[textRow.key] ?? "") !== text)
                    pages.dialog.projectPut(textRow.key, text)
            }
            Component.onCompleted: refresh()
            Connections {
                target: pages.dialog
                function onProjectValuesChanged() { if (!field.activeFocus) field.refresh() }
            }
            onTextEdited: typing.restart()
            onEditingFinished: commit()
            Timer {
                id: typing
                interval: 500
                onTriggered: field.commit()
            }
        }
    }

    Page {
        page: "lineEditor"
        Section {
            key: "lineEditor.translationMode"
            ProjectCheck { setting: "translation.autoMoveTagsFromOriginal" }
            ProjectField { setting: "translation.sourceLanguage"; placeholder: qsTr("Detect") }
            ProjectField {
                setting: "translation.targetLanguage"
                placeholder: pages.dialog.projectYours["ai.inline.translateTo"] ?? ""
            }
            ProjectValue { setting: "translation.glossary" }
        }
    }
    Page {
        page: "spelling"
        Section {
            key: "spelling.spelling"
            ProjectCheck { setting: "editor.spellchecker" }
            ProjectChoice { setting: "editor.dictionaryLanguage"; model: pages.dialog.dictionaries }
        }
        Section {
            key: "spelling.grammar"
            ProjectChoice {
                setting: "grammar.language"
                model: [qsTranslate("HikariSub.Options", "Off"), qsTranslate("HikariSub.Options", "English (US)"),
                        qsTranslate("HikariSub.Options", "English (UK)"), qsTranslate("HikariSub.Options", "English (Canada)"),
                        qsTranslate("HikariSub.Options", "English (Australia)")]
            }
        }
        Section {
            key: "spelling.ruleGroups"
            Repeater {
                model: pages.app.grammarGroups()
                ProjectCheck {
                    required property var modelData
                    setting: "grammar.group." + modelData.id
                    text: modelData.name
                }
            }
        }
    }
    Page {
        page: "conversion"
        Section {
            key: "conversion.export"
            SettingsRow {
                host: pages.dialog
                project: true
                setting: "export.format"
                ComboBox {
                    id: exportFormat
                    objectName: "project_export.format"
                    model: pages.exportFormats
                    textRole: "text"
                    Layout.preferredWidth: 260
                    Accessible.name: SettingsCatalogue.label("export.format")
                    function refresh() {
                        currentIndex = Math.max(0, pages.exportFormats.findIndex(f => f.id === (pages.dialog.projectValues["export.format"] ?? "")))
                    }
                    Component.onCompleted: refresh()
                    Connections { target: pages.dialog; function onProjectValuesChanged() { exportFormat.refresh() } }
                    onActivated: index => pages.dialog.projectPut("export.format", pages.exportFormats[index].id)
                }
            }
        }
    }
    Page {
        page: "subtitleProperties"
        Section {
            key: "subtitleProperties.credits"
            Label {
                text: qsTr("Saving a file writes the switched-on fields into its Script Info.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: pages.scriptFields
                ProjectText {}
            }
            ProjectCheck { setting: "scriptProperties.askForChange" }
        }
    }
    Page {
        page: "ai"
        Section {
            key: "ai.models"
            ProjectValue { setting: "translation.translator" }
            ProjectValue { setting: "translation.reviewer" }
        }
        Section {
            key: "ai.privacy"
            ProjectValue { setting: "ai.exclude" }
        }
        Section {
            key: "ai.keys"
            ProjectAiKeysPage {
                id: aiKeys
                app: pages.app
                shellWindow: pages.shellWindow
            }
        }
    }
}
