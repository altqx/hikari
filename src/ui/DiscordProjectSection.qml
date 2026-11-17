pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Project"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: section
    objectName: "projectDiscord"
    required property var project
    readonly property var choices: [{value: "yours", text: qsTr("Use yours")},
                                    {value: "hide", text: qsTr("Hide presence while this project is open")},
                                    {value: "own", text: qsTr("Use this project’s text")}]
    contentWidth: availableWidth

    component Origin: RowLayout {
        id: origin
        required property string key
        readonly property string state: section.project.stateOf(key)
        spacing: 4
        Label {
            objectName: "projectState_" + origin.key
            color: Theme.muted
            text: origin.state === "local" ? qsTr("Set in project.local.toml")
                : origin.state === "project" ? qsTr("Set in this project") : qsTr("Uses yours")
        }
        IconToolButton {
            objectName: "projectUseYours_" + origin.key
            visible: origin.state === "project"
            iconRole: "restore-default"
            text: qsTr("Use yours")
            onClicked: section.project.useYours(origin.key)
        }
    }
    component Field: RowLayout {
        id: row
        required property string key
        required property string name
        Layout.fillWidth: true
        Label { text: row.name; Layout.preferredWidth: 160 }
        TextField {
            id: field
            objectName: "project_" + row.key
            enabled: section.project.stateOf(row.key) !== "local"
            Layout.fillWidth: true
            Accessible.name: row.name
            function refresh() { text = section.project.values[row.key] ?? "" }
            Component.onCompleted: refresh()
            Connections { target: section.project; function onReloaded() { field.refresh() } }
            onTextEdited: section.project.put(row.key, text)
        }
        Origin { key: row.key }
    }

    ColumnLayout {
        x: 24
        y: 20
        width: parent.width - 48
        spacing: 10
        Label { text: qsTr("Discord"); font.bold: true; color: Theme.text }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("In this project"); Layout.preferredWidth: 160 }
            ComboBox {
                id: choice
                objectName: "project_privacy.discordProject"
                model: section.choices
                textRole: "text"
                enabled: section.project.stateOf("privacy.discordProject") !== "local"
                Layout.preferredWidth: 320
                Accessible.name: qsTr("In this project")
                function refresh() {
                    currentIndex = Math.max(0, section.choices.findIndex(
                        c => c.value === (section.project.values["privacy.discordProject"] ?? "yours")))
                }
                Component.onCompleted: refresh()
                Connections { target: section.project; function onReloaded() { choice.refresh() } }
                onActivated: index => section.project.put("privacy.discordProject", section.choices[index].value)
            }
            Item { Layout.fillWidth: true }
            Origin { key: "privacy.discordProject" }
        }
        ColumnLayout {
            visible: (section.project.values["privacy.discordProject"] ?? "yours") === "own"
            spacing: 10
            Layout.fillWidth: true
            Field { key: "privacy.discordShow"; name: qsTr("Show name") }
            Field { key: "privacy.discordDetails"; name: qsTr("Details (first line)") }
            Field { key: "privacy.discordState"; name: qsTr("State (second line)") }
            Label {
                text: qsTr("An empty line uses yours. Until you trust this folder, your own presence shows.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Label {
            text: qsTr("What shows stays yours: a project can’t show the file or project name.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
