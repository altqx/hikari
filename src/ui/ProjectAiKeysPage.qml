pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Project"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: page
    objectName: "projectAiKeys"
    required property var app
    required property int shellWindow
    property var state: ({})
    property var names: ({})
    property var edited: ({})
    signal reloaded()
    contentWidth: availableWidth

    function load() {
        state = app.projectAiKeys(shellWindow)
        const n = {}
        for (const row of state.rows ?? [])
            n[row.id] = {env: row.env, entry: row.entry}
        names = n
        edited = {}
        reloaded()
    }
    function put(id, field, value) {
        const pair = Object.assign({}, names[id] ?? {env: "", entry: ""}, {[field]: value})
        names = Object.assign({}, names, {[id]: pair})
        edited = Object.assign({}, edited, {[id]: pair})
    }
    function useYours(id) {
        put(id, "env", "")
        put(id, "entry", "")
        reloaded()
    }
    function apply() {
        if (Object.keys(edited).length === 0)
            return true
        const ok = app.applyProjectAiKeys(shellWindow, edited)
        load()
        return ok
    }
    function usedText(row) {
        switch (row.used) {
        case "env": return qsTr("From %1").arg(row.from)
        case "file": return qsTr("From the credentials file")
        case "untrusted": return qsTr("Yours. Project not trusted")
        case "noFile": return qsTr("No credentials file. Uses yours")
        case "notFound": return qsTr("Not found. Uses yours")
        case "noKey": return qsTr("Needs no key")
        }
        return qsTr("Uses yours")
    }

    ColumnLayout {
        x: 24
        y: 20
        width: parent.width - 48
        spacing: 10

        InlineBar {
            objectName: "projectAiKeysValues"
            visible: (page.state.valuesFound ?? []).length > 0
            severity: "warning"
            closable: false
            text: qsTr("project.toml holds an AI key for %1, which is ignored. Keys go in the credentials file. If project.toml was committed or shared, replace the key at the provider too.")
                  .arg((page.state.valuesFound ?? []).join(", "))
            Layout.fillWidth: true
            Button {
                objectName: "projectAiKeysMove"
                text: qsTr("Move to credentials file")
                onClicked: {
                    page.app.moveProjectKeysToCredentials(page.shellWindow)
                    page.load()
                }
            }
        }
        InlineBar {
            objectName: "projectAiKeysUntrusted"
            visible: page.state.trusted === false
            iconRole: "untrusted"
            closable: false
            text: qsTr("“%1” isn’t trusted, so its keys aren’t used. Hikari AI uses yours in this window.")
                  .arg(page.app.projectName(page.shellWindow))
            Layout.fillWidth: true
            IconTextButton {
                objectName: "projectAiKeysTrust"
                iconRole: "trust"
                text: qsTr("Trust folder")
                onClicked: {
                    page.app.trustProject(page.shellWindow)
                    page.load()
                }
            }
        }
        Label {
            text: qsTr("A key from this machine’s environment or from this Project’s credentials file replaces yours, only in this Project’s windows. The environment comes first. Base URLs, budgets and on/off stay yours.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ColumnLayout {
            objectName: "projectAiKeysTable"
            spacing: 6
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Label { text: qsTr("Provider"); color: Theme.muted; font.bold: true; Layout.preferredWidth: 140 }
                Label { text: qsTr("Environment variable"); color: Theme.muted; font.bold: true; Layout.preferredWidth: 160 }
                Label { text: qsTr("Credentials entry"); color: Theme.muted; font.bold: true; Layout.preferredWidth: 120 }
                Label { text: qsTr("Key used"); color: Theme.muted; font.bold: true; Layout.fillWidth: true }
            }
            Repeater {
                model: page.state.rows ?? []
                delegate: RowLayout {
                    id: row
                    required property var modelData
                    readonly property bool editable: modelData.needsKey && modelData.known
                    spacing: 8
                    Layout.fillWidth: true
                    Label {
                        text: row.modelData.known ? row.modelData.name : row.modelData.id
                        color: row.editable ? Theme.text : Theme.disabled
                        elide: Text.ElideRight
                        Layout.preferredWidth: 140
                    }
                    TextField {
                        id: env
                        objectName: "projectAiKeyEnv_" + row.modelData.id
                        enabled: row.editable
                        placeholderText: qsTr("None")
                        Accessible.name: qsTr("Environment variable for %1").arg(row.modelData.name || row.modelData.id)
                        Layout.preferredWidth: 160
                        function refresh() { text = page.names[row.modelData.id]?.env ?? "" }
                        Component.onCompleted: refresh()
                        Connections { target: page; function onReloaded() { env.refresh() } }
                        onTextEdited: page.put(row.modelData.id, "env", text)
                    }
                    TextField {
                        id: entry
                        objectName: "projectAiKeyEntry_" + row.modelData.id
                        enabled: row.editable
                        placeholderText: qsTr("None")
                        Accessible.name: qsTr("Credentials entry for %1").arg(row.modelData.name || row.modelData.id)
                        Layout.preferredWidth: 120
                        function refresh() { text = page.names[row.modelData.id]?.entry ?? "" }
                        Component.onCompleted: refresh()
                        Connections { target: page; function onReloaded() { entry.refresh() } }
                        onTextEdited: page.put(row.modelData.id, "entry", text)
                    }
                    RowLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Label {
                            objectName: "projectAiKeyUsed_" + row.modelData.id
                            text: page.usedText(row.modelData)
                            color: row.modelData.used === "env" || row.modelData.used === "file" ? Theme.success
                                 : row.modelData.used === "noFile" || row.modelData.used === "notFound" ? Theme.warning
                                 : Theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        IconToolButton {
                            objectName: "projectAiKeyUseYours_" + row.modelData.id
                            visible: (page.names[row.modelData.id]?.env ?? "").length > 0 || (page.names[row.modelData.id]?.entry ?? "").length > 0
                            iconRole: "restore-default"
                            text: qsTr("Use yours")
                            onClicked: page.useYours(row.modelData.id)
                        }
                    }
                }
            }
        }
        RowLayout {
            spacing: 8
            Layout.topMargin: 6
            Layout.fillWidth: true
            Label { text: qsTr("Credentials file"); Layout.preferredWidth: 120 }
            Label {
                objectName: "projectCredentialsState"
                text: page.state.credentialsExists ? qsTr("%1 · git ignores it").arg(".hikari/credentials.toml")
                                                   : qsTr("%1 · not created").arg(".hikari/credentials.toml")
                color: Theme.muted
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            IconTextButton {
                objectName: "projectCredentialsCreate"
                visible: !page.state.credentialsExists
                iconRole: "add"
                text: qsTr("Create credentials file")
                onClicked: {
                    page.apply()
                    page.app.createProjectCredentials(page.shellWindow)
                    page.load()
                }
            }
            Button {
                objectName: "projectCredentialsOpen"
                visible: page.state.credentialsExists === true
                text: qsTr("Open file")
                onClicked: page.app.openProjectCredentials(page.shellWindow)
            }
            IconTextButton {
                objectName: "projectCredentialsReveal"
                visible: page.state.credentialsExists === true
                iconRole: "show-in-folder"
                text: qsTr("Show in folder")
                onClicked: page.app.showInFolder(page.state.credentialsPath)
            }
        }
        Label {
            visible: !page.state.credentialsExists
            text: qsTr("Creates the file and .hikari/.gitignore, which keeps it and project.local.toml out of git. Keys never reach Hikari AI or the model.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
