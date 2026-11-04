pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: page
    objectName: "settingsPageAi"
    required property var dialog
    readonly property var ai: dialog.ai
    readonly property bool stagedOff: dialog.values["ai.off"] === true
    readonly property bool live: !stagedOff && !!ai && ai.on
    readonly property var providerList: ai ? ai.providers : []
    property string adding: ""
    property string editing: ""
    property var stagedRoles: ({})
    readonly property var codex: ai ? ai.codex : ({})
    property bool codexBrowser: false
    readonly property var roleNames: [
        {id: "chat", name: qsTr("Chat")},
        {id: "translator", name: qsTr("Translator")},
        {id: "reviewer", name: qsTr("Reviewer")},
        {id: "vision", name: qsTr("Vision")},
        {id: "judge", name: qsTr("Judge")}
    ]
    readonly property var presets: [
        {id: "openai", name: "OpenAI", iconRole: "ai-assistant"},
        {id: "anthropic", name: "Anthropic", iconRole: "ai-assistant"},
        {id: "gemini", name: "Google Gemini", iconRole: "ai-assistant"},
        {id: "openrouter", name: "OpenRouter", iconRole: "ai-assistant"},
        {id: "custom", name: qsTr("Custom endpoint…"), iconRole: "link"}
    ]
    contentWidth: availableWidth
    contentHeight: column.implicitHeight + 44

    function reload() {
        stagedRoles = ai ? Object.assign({}, ai.roles) : {}
        closeForm()
        if (ai && ai.on)
            ai.checkCodex()
    }
    function signInCodex(browser) {
        codexBrowser = browser
        ai.signInCodex(browser)
    }
    function commit() {
        if (ai && ai.on)
            ai.applyRoles(stagedRoles)
    }
    function stage(role, value) {
        const r = Object.assign({}, stagedRoles)
        r[role] = value
        stagedRoles = r
    }
    function openForm(preset, id) {
        adding = id ? "" : preset
        editing = id ?? ""
        ai.openDraft()
    }
    function closeForm() {
        if (ai && (adding !== "" || editing !== ""))
            ai.closeDraft()
        adding = ""
        editing = ""
    }
    function askDelete(row) {
        dialog.ask({
            title: qsTr("Delete provider"),
            text: qsTr("Delete the provider “%1”?").arg(row.name),
            detail: row.needsKey ? qsTr("Its key is removed from the keychain. Roles that use its models go back to the chat model.")
                                 : qsTr("Roles that use its models go back to the chat model."),
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, answer => { if (answer === "delete") page.ai.remove(row.id) })
    }
    function wireName(wire) {
        switch (wire) {
        case "chat-completions": return qsTr("Chat Completions")
        case "responses": return qsTr("Responses")
        case "anthropic-messages": return qsTr("Messages")
        case "gemini": return qsTr("Gemini API")
        }
        return wire
    }
    function endpointText(row) {
        if (row.preset === "custom")
            return row.baseUrl
        const host = row.baseUrl.replace(/^[a-z]+:\/\//, "").replace(/\/.*$/, "")
        return host
    }
    function signInText(kind, host) {
        switch (kind) {
        case "denied": return host === "openrouter.ai" ? qsTr("openrouter.ai refused the sign-in.")
                                                       : qsTr("auth.openai.com refused the sign-in. Approve HikariSub’s request with the ChatGPT account you want, then try again.")
        case "codesOff": return qsTr("Your ChatGPT workspace turns off sign-in codes. Sign in through the browser instead.")
        case "codeExpired": return qsTr("The code expired before it was entered.")
        case "timedOut": return qsTr("Sign-in timed out waiting for the browser.")
        case "portBusy": return qsTr("Browser sign-in isn’t available while another app uses port 1455. Close that app’s sign-in to use it.")
        case "network": return qsTr("Couldn’t reach %1. Check the connection.").arg(host)
        case "keychain": return failureText("keychainDenied", 0)
        case "import": return qsTr("~/.codex has no ChatGPT sign-in to import.")
        case "sidecar":
        case "invalid": return qsTr("Sign-in failed. Try again.")
        }
        return failureText(kind, 0)
    }
    function countdown(expiresAt, now) {
        const left = Math.max(0, Math.floor((expiresAt - now) / 1000))
        return Math.floor(left / 60) + ":" + String(left % 60).padStart(2, "0")
    }
    function failureText(error, httpStatus, detail) {
        switch (error) {
        case "signIn": return signInText(detail, "openrouter.ai")
        case "unauthorized": return qsTr("Key rejected (%1)").arg(httpStatus)
        case "rateLimited": return qsTr("Too many requests (%1)").arg(httpStatus)
        case "rejected": return qsTr("Request refused (%1)").arg(httpStatus)
        case "server": return qsTr("Provider error (%1)").arg(httpStatus)
        case "network": return qsTr("Can’t reach the provider")
        case "timeout": return qsTr("No answer from the provider")
        case "malformed": return qsTr("Unexpected answer")
        case "insecureKey": return qsTr("A key goes only over HTTPS, to this computer or to a Tailscale peer")
        case "key": return qsTr("Enter a key")
        case "keychainUnavailable": return qsTr("No system keychain is available")
        case "keychainDenied": return qsTr("Access to the keychain was refused")
        case "config": return qsTr("Can’t save the provider list")
        case "form": return qsTr("Fill in the name and a valid address")
        case "downloadManifest":
        case "downloadNetwork": return qsTr("Can’t download the AI component")
        case "downloadDamaged": return qsTr("Downloaded AI component is damaged")
        case "downloadDisk": return qsTr("Can’t save the AI component")
        }
        return qsTr("Test failed")
    }
    function megabytes(bytes) { return Math.max(1, Math.round(bytes / 1000000)) }

    Connections {
        target: page.dialog
        function onReloaded() { page.reload() }
    }
    Connections {
        target: page.ai
        function onDraftSaved() { page.adding = ""; page.editing = "" }
        function onSwitched() { page.reload() }
    }

    component AiCheck: CheckBox {
        id: check
        required property string setting
        objectName: "setting_" + setting
        leftPadding: 1
        spacing: 9
        Layout.fillWidth: true
        Accessible.name: text
        function refresh() { checked = page.dialog.values[setting] === true }
        Component.onCompleted: {
            if (contentItem && contentItem.wrapMode !== undefined)
                contentItem.wrapMode = Text.Wrap
            refresh()
        }
        Connections { target: page.dialog; function onReloaded() { check.refresh() } }
        onToggled: page.dialog.put(setting, checked)
    }
    component Note: Label {
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.leftMargin: 24
        Layout.topMargin: -5
        Layout.fillWidth: true
    }
    component BusyIcon: Icon {
        id: busyIcon
        iconRole: "run-busy"
        RotationAnimator on rotation {
            running: busyIcon.visible
            from: 0
            to: 360
            duration: 1200
            loops: Animation.Infinite
        }
    }
    component StatusDot: Rectangle {
        property string status
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: status === "ok" ? Theme.success : status === "failed" ? Theme.danger : Theme.muted
    }

    component ProviderForm: Rectangle {
        id: form
        required property string preset
        property string providerId: ""
        readonly property bool editingOne: providerId !== ""
        readonly property var existing: editingOne ? page.ai.form(providerId) : ({})
        readonly property bool custom: preset === "custom"
        readonly property var draft: page.ai.draft
        readonly property var download: page.ai.download
        readonly property bool busy: draft.state === "testing" || draft.state === "saving" || draft.state === "signingIn"
        readonly property bool openRouter: preset === "openrouter"
        readonly property bool signedIn: editingOne && existing.signedIn === true
        objectName: "aiProviderForm"
        color: Theme.panel
        border.color: Theme.accent
        border.width: 1
        radius: 2
        implicitHeight: formColumn.implicitHeight + 24
        Layout.fillWidth: true

        function values() {
            return { id: providerId, preset: preset, name: nameField.text, baseUrl: urlField.text,
                     wire: wireChoice.currentIndex === 1 ? "responses" : "chat-completions", key: keyField.text }
        }
        Component.onCompleted: {
            const name = page.presets.find(p => p.id === preset)
            nameField.text = editingOne ? existing.name : custom ? "" : (name ? name.name : "")
            urlField.text = editingOne ? existing.baseUrl : ""
            wireChoice.currentIndex = editingOne && existing.wire === "responses" ? 1 : 0
            if (custom && !editingOne)
                urlField.forceActiveFocus()
            else
                keyField.forceActiveFocus()
        }

        ColumnLayout {
            id: formColumn
            x: 12
            y: 12
            width: form.width - 24
            spacing: 8
            Label {
                text: form.editingOne ? qsTr("Edit “%1”").arg(form.existing.name)
                                      : form.custom ? qsTr("Add a custom endpoint")
                                                    : qsTr("Add %1").arg(page.presets.find(p => p.id === form.preset).name)
                font.bold: true
                Layout.fillWidth: true
            }
            SettingsRow {
                text: qsTr("Name")
                labelWidth: 150
                TextField { id: nameField; objectName: "aiProviderName"; Layout.fillWidth: true; Accessible.name: qsTr("Name") }
            }
            SettingsRow {
                visible: form.custom
                text: qsTr("Base URL")
                labelWidth: 150
                TextField {
                    id: urlField
                    objectName: "aiProviderBaseUrl"
                    placeholderText: "http://localhost:11434/v1"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Base URL")
                }
            }
            SettingsRow {
                visible: form.custom
                text: qsTr("API")
                labelWidth: 150
                ComboBox {
                    id: wireChoice
                    objectName: "aiProviderWire"
                    model: [qsTr("Chat Completions"), qsTr("Responses")]
                    Accessible.name: qsTr("API")
                }
            }
            SettingsRow {
                id: keyRow
                objectName: "aiProviderKeyRow"
                text: form.preset === "gemini" ? qsTr("Gemini API key")
                                               : form.openRouter ? qsTr("OpenRouter key")
                                                                 : form.custom ? qsTr("API key (optional)") : qsTr("API key")
                labelWidth: 150
                Button {
                    objectName: "aiOpenRouterSignIn"
                    visible: form.openRouter && !form.signedIn
                    text: qsTr("Sign in with OpenRouter…")
                    enabled: !form.busy && nameField.text.trim().length > 0
                    onClicked: page.ai.signInDraft(form.values())
                }
                Label {
                    objectName: "aiOpenRouterSignedIn"
                    visible: form.signedIn
                    text: qsTr("Signed in (OpenRouter)")
                }
                Button {
                    objectName: "aiOpenRouterSignOut"
                    visible: form.signedIn
                    text: qsTr("Sign out")
                    enabled: !form.busy
                    onClicked: {
                        page.ai.signOutProvider(form.providerId)
                        page.closeForm()
                    }
                }
                Label {
                    visible: form.openRouter
                    text: qsTr("or paste a key")
                    color: Theme.muted
                }
                TextField {
                    id: keyField
                    objectName: "aiProviderKey"
                    echoMode: showKey.checked ? TextInput.Normal : TextInput.Password
                    placeholderText: form.editingOne && form.existing.needsKey ? qsTr("Saved in the keychain. Type a new key to replace it")
                                                                                 : form.openRouter ? "sk-or-v1-…" : ""
                    Layout.fillWidth: true
                    Accessible.name: keyRow.text
                    onAccepted: if (!form.busy) page.ai.testDraft(form.values())
                }
                IconToolButton {
                    id: showKey
                    iconRole: checked ? "hide" : "show"
                    text: checked ? qsTr("Hide key") : qsTr("Show key")
                    checkable: true
                }
            }
            RowLayout {
                visible: form.preset === "gemini"
                spacing: 8
                Layout.leftMargin: 182
                Layout.fillWidth: true
                LinkButton {
                    objectName: "aiGeminiKeys"
                    iconRole: "open-link"
                    text: qsTr("Get a key in Google AI Studio")
                    onClicked: page.ai.openGeminiKeys()
                }
            }
            Label {
                visible: form.preset === "gemini"
                text: qsTr("A key from Google AI Studio. Vertex AI and Google Cloud sign-in aren’t supported.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 182
                Layout.fillWidth: true
            }
            Label {
                visible: form.openRouter && !form.signedIn
                text: qsTr("Sign-in opens openrouter.ai in your browser and makes a key for HikariSub. It goes to the keychain, like a pasted key.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 182
                Layout.fillWidth: true
            }
            Label {
                objectName: "aiOpenRouterSignOutNote"
                visible: form.signedIn
                textFormat: Text.StyledText
                linkColor: Theme.accent
                text: qsTr("Sign out deletes the key from this computer. It stays in %1 until you delete it there.")
                      .arg("<a href=\"keys\">" + qsTr("your OpenRouter keys") + "</a>")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 182
                Layout.fillWidth: true
                onLinkActivated: page.ai.openOpenRouterKeys()
                HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
            Rectangle {
                objectName: "aiOpenRouterWaiting"
                visible: form.draft.state === "signingIn"
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: waitingRow.implicitHeight + 16
                Layout.leftMargin: 182
                Layout.fillWidth: true
                RowLayout {
                    id: waitingRow
                    x: 10
                    y: 8
                    width: parent.width - 20
                    spacing: 8
                    BusyIcon {}
                    Label {
                        text: qsTr("Waiting for openrouter.ai in your browser")
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Button {
                        objectName: "aiOpenRouterOpenAgain"
                        text: qsTr("Open the page again")
                        onClicked: page.ai.openSignInPage()
                    }
                    Button {
                        objectName: "aiOpenRouterCancel"
                        text: qsTr("Cancel")
                        onClicked: page.ai.cancelSignIn()
                    }
                }
            }
            Label {
                visible: form.custom
                text: qsTr("A local server such as Ollama or LM Studio needs no key. A key is sent only over HTTPS, to this computer or to a Tailscale peer.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 182
                Layout.fillWidth: true
            }
            Rectangle {
                objectName: "aiDownloadNotice"
                visible: form.download.needed
                color: Theme.raised
                border.color: Theme.line
                implicitHeight: noticeRow.implicitHeight + 16
                Layout.fillWidth: true
                Rectangle { width: 3; height: parent.height; color: Theme.accent }
                RowLayout {
                    id: noticeRow
                    x: 12
                    y: 8
                    width: parent.width - 22
                    spacing: 8
                    Icon { iconRole: "notice-info"; Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Label {
                            text: form.download.size > 0
                                  ? qsTr("Your first provider downloads the AI component once: %1 MB.").arg(page.megabytes(form.download.size))
                                  : qsTr("Your first provider downloads the AI component once.")
                            font.bold: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Test connection and Add download it. It runs only when you use an AI feature, and never while AI is turned off.")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            visible: form.download.active
                            spacing: 8
                            ProgressBar {
                                objectName: "aiDownloadProgress"
                                from: 0
                                to: Math.max(1, form.download.size)
                                value: form.download.received
                                Layout.preferredWidth: 180
                            }
                            Label {
                                text: qsTr("%1 of %2 MB").arg(Math.round(form.download.received / 1000000), page.megabytes(form.download.size))
                                color: Theme.muted
                            }
                        }
                    }
                }
            }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Layout.topMargin: 4
                StatusDot { status: form.draft.state ?? ""; visible: (form.draft.state ?? "") !== "" }
                Label {
                    objectName: "aiDraftStatus"
                    text: {
                        switch (form.draft.state) {
                        case "testing": return form.download.active ? qsTr("Tests when the download finishes") : qsTr("Testing…")
                        case "saving": return qsTr("Saving…")
                        case "ok": return qsTr("Connected · %n model(s)", "", form.draft.models)
                        case "failed": return page.failureText(form.draft.error, form.draft.httpStatus, form.draft.detail)
                        }
                        return ""
                    }
                    color: form.draft.state === "ok" ? Theme.success : form.draft.state === "failed" ? Theme.danger : Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Button {
                    objectName: "aiDraftTest"
                    text: qsTr("Test connection")
                    enabled: !form.busy
                    onClicked: page.ai.testDraft(form.values())
                }
                Button {
                    objectName: "aiDraftCancel"
                    text: qsTr("Cancel")
                    onClicked: page.closeForm()
                }
                PrimaryButton {
                    objectName: "aiDraftSave"
                    text: form.editingOne ? qsTr("Save") : qsTr("Add")
                    enabled: !form.busy && nameField.text.trim().length > 0
                    onClicked: page.ai.saveDraft(form.values())
                }
            }
        }
    }

    ColumnLayout {
        id: column
        x: 24
        y: 20
        width: page.availableWidth - 48
        spacing: 16

        SettingsSection {
            title: qsTr("AI features")
            ruled: false
            AiCheck { setting: "ai.off"; text: qsTr("Turn off all AI features"); font.bold: true }
            Note { text: qsTr("Nothing AI-related shows, downloads, starts or reads your keychain. Providers, keys and threads are kept, and turning AI back on brings them back without a restart.") }
        }

        ColumnLayout {
            objectName: "aiOffSummary"
            visible: page.stagedOff
            spacing: 12
            Layout.fillWidth: true
            Repeater {
                model: [
                    {title: qsTr("Kept while AI is off"),
                     items: [qsTr("Your providers, and their keys in the keychain"), qsTr("Your agent threads"),
                             qsTr("Downloaded AI component")]},
                    {title: qsTr("Hidden while AI is off"),
                     items: [qsTr("The agent panel, inline AI actions and AI rows in every menu"),
                             qsTr("AI commands in command search, their shortcuts and toolbar buttons"),
                             qsTr("AI in Getting started and What’s new"),
                             qsTr("MCP server, which stops listening")]}
                ]
                delegate: Rectangle {
                    id: box
                    required property var modelData
                    color: "transparent"
                    border.color: Theme.line
                    radius: 2
                    implicitHeight: boxColumn.implicitHeight + 20
                    Layout.fillWidth: true
                    ColumnLayout {
                        id: boxColumn
                        x: 12
                        y: 10
                        width: box.width - 24
                        spacing: 3
                        Label { text: box.modelData.title; color: Theme.muted; font.bold: true }
                        Repeater {
                            model: box.modelData.items
                            delegate: Label {
                                required property string modelData
                                text: "•  " + modelData
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        Label {
            objectName: "aiApplyToTurnOn"
            visible: !page.stagedOff && !!page.ai && !page.ai.on
            text: qsTr("Press OK or Apply to turn AI back on.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        SettingsSection {
            objectName: "aiProviders"
            visible: page.live
            title: qsTr("Providers")
            Rectangle {
                color: "transparent"
                border.color: Theme.line
                radius: 2
                visible: page.providerList.length > 0 || page.adding === ""
                implicitHeight: providerColumn.implicitHeight + 2
                Layout.fillWidth: true
                ColumnLayout {
                    id: providerColumn
                    x: 1
                    y: 1
                    width: parent.width - 2
                    spacing: 0
                    Label {
                        objectName: "aiNoProviders"
                        visible: page.providerList.length === 0
                        text: qsTr("No providers yet. AI sends nothing until you add one.")
                        color: Theme.muted
                        leftPadding: 12
                        topPadding: 12
                        bottomPadding: 12
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: page.providerList
                        delegate: ColumnLayout {
                            id: row
                            required property var modelData
                            required property int index
                            objectName: "aiProvider_" + modelData.id
                            spacing: 0
                            Layout.fillWidth: true
                            Rectangle { visible: row.index > 0; color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
                            Loader {
                                active: page.editing === row.modelData.id
                                visible: active
                                Layout.fillWidth: true
                                Layout.margins: 6
                                sourceComponent: ProviderForm {
                                    preset: row.modelData.preset
                                    providerId: row.modelData.id
                                }
                            }
                            RowLayout {
                                visible: page.editing !== row.modelData.id
                                spacing: 8
                                Layout.fillWidth: true
                                Layout.margins: 6
                                Layout.leftMargin: 10
                                ColumnLayout {
                                    spacing: 1
                                    Layout.fillWidth: true
                                    Label { text: row.modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label {
                                        text: page.wireName(row.modelData.wire) + " · " + page.endpointText(row.modelData)
                                        color: Theme.muted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                ColumnLayout {
                                    spacing: 1
                                    RowLayout {
                                        spacing: 5
                                        Layout.alignment: Qt.AlignRight
                                        StatusDot { status: row.modelData.state }
                                        Label {
                                            objectName: "aiProviderStatus"
                                            text: {
                                                switch (row.modelData.state) {
                                                case "testing": return qsTr("Testing…")
                                                case "ok": return qsTr("Connected · %n model(s)", "", row.modelData.models)
                                                case "failed": return page.failureText(row.modelData.error, row.modelData.httpStatus)
                                                }
                                                return qsTr("Not tested · %n model(s)", "", row.modelData.models)
                                            }
                                            color: row.modelData.state === "ok" ? Theme.success
                                                 : row.modelData.state === "failed" ? Theme.danger : Theme.muted
                                        }
                                    }
                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        objectName: "aiProviderKeyNote"
                                        text: row.modelData.state === "failed" && row.modelData.error === "unauthorized"
                                              ? (row.modelData.signedIn ? qsTr("Sign in again with Edit") : qsTr("Replace the key with Edit"))
                                              : row.modelData.signedIn ? qsTr("Signed in (OpenRouter)")
                                                                       : row.modelData.needsKey ? qsTr("Key in keychain") : qsTr("No key")
                                        color: Theme.muted
                                    }
                                }
                                Button {
                                    objectName: "aiProviderTest"
                                    text: qsTr("Test connection")
                                    enabled: row.modelData.state !== "testing"
                                    onClicked: page.ai.test(row.modelData.id)
                                }
                                IconToolButton {
                                    objectName: "aiProviderEdit"
                                    iconRole: "edit"
                                    text: qsTr("Edit")
                                    onClicked: page.openForm(row.modelData.preset, row.modelData.id)
                                }
                                IconToolButton {
                                    objectName: "aiProviderDelete"
                                    iconRole: "delete"
                                    text: qsTr("Delete")
                                    onClicked: page.askDelete(row.modelData)
                                }
                            }
                        }
                    }
                }
            }
            Loader {
                active: page.adding !== ""
                visible: active
                Layout.fillWidth: true
                sourceComponent: ProviderForm { preset: page.adding }
            }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                IconTextButton {
                    id: addButton
                    objectName: "aiAddProvider"
                    iconRole: "add"
                    text: qsTr("Add provider")
                    enabled: page.adding === "" && page.editing === ""
                    onClicked: addMenu.popup(addButton, 0, addButton.height)
                }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Changes to providers and keys apply at once."); color: Theme.muted }
                ShellMenu {
                    id: addMenu
                    objectName: "aiAddMenu"
                    Repeater {
                        model: page.presets
                        delegate: ShellMenuItem {
                            required property var modelData
                            objectName: "aiAdd_" + modelData.id
                            iconRole: modelData.iconRole
                            text: modelData.name
                            onTriggered: page.openForm(modelData.id)
                        }
                    }
                }
            }
        }

        SettingsSection {
            objectName: "aiModels"
            visible: page.live
            title: qsTr("Models")
            IconTextButton {
                objectName: "aiRefreshModels"
                iconRole: "refresh"
                text: qsTr("Refresh models")
                enabled: page.providerList.length > 0
                Layout.alignment: Qt.AlignRight
                onClicked: page.ai.refreshModels()
            }
            Repeater {
                model: page.roleNames
                delegate: SettingsRow {
                    id: roleRow
                    required property var modelData
                    readonly property bool chat: modelData.id === "chat"
                    readonly property var choices: {
                        const all = page.ai ? page.ai.modelChoices : []
                        const usable = modelData.id === "vision" ? all.filter(c => c.vision) : all
                        const listed = usable.map(c => ({ value: c.value, text: c.text + " · " + c.provider }))
                        return chat ? listed : [{ value: "", text: qsTr("Same as chat") }].concat(listed)
                    }
                    text: modelData.name
                    labelWidth: 200
                    ComboBox {
                        id: roleChoice
                        objectName: "aiRole_" + roleRow.modelData.id
                        model: roleRow.choices.length > 0 ? roleRow.choices : [{ value: "", text: qsTr("Add a provider first") }]
                        textRole: "text"
                        valueRole: "value"
                        enabled: page.providerList.length > 0
                        function sync() {
                            currentIndex = Math.max(0, roleRow.choices.findIndex(c => c.value === (page.stagedRoles[roleRow.modelData.id] ?? "")))
                        }
                        Component.onCompleted: sync()
                        onModelChanged: sync()
                        Connections { target: page; function onStagedRolesChanged() { roleChoice.sync() } }
                        onActivated: index => page.stage(roleRow.modelData.id, valueAt(index))
                        Accessible.name: roleRow.modelData.name
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                    }
                }
            }
            Note {
                text: qsTr("A role without its own model uses the chat model. Vision lists only models that read images.")
                Layout.leftMargin: 0
                Layout.topMargin: 0
            }
        }

        SettingsSection {
            objectName: "aiExternalAgents"
            visible: page.live
            title: qsTr("External agents")
            Rectangle {
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: codexColumn.implicitHeight + 20
                Layout.fillWidth: true
                ColumnLayout {
                    id: codexColumn
                    objectName: "aiCodex"
                    readonly property string state: page.codex.state ?? ""
                    readonly property string error: page.codex.error ?? ""
                    readonly property bool panel: state !== "" || (error !== "" && error !== "expired")
                    x: 10
                    y: 10
                    width: parent.width - 20
                    spacing: 6
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Label { text: "Codex"; font.bold: true }
                        Label {
                            text: page.codex.path ?? ""
                            textFormat: Text.PlainText
                            color: Theme.muted
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        objectName: "aiCodexFound"
                        textFormat: Text.PlainText
                        text: !page.codex.found ? qsTr("Not found on PATH.")
                                                : page.codex.version ? qsTr("Found on PATH · %1.").arg(page.codex.version)
                                                                     : qsTr("Found on PATH.")
                        color: Theme.muted
                        Layout.fillWidth: true
                    }
                    ColumnLayout {
                        objectName: "aiCodexSignedOut"
                        visible: !codexColumn.panel && !page.codex.signedIn && codexColumn.error === ""
                        spacing: 6
                        Layout.fillWidth: true
                        Label { text: qsTr("Not signed in") }
                        RowLayout {
                            spacing: 8
                            Button {
                                objectName: "aiCodexSignIn"
                                text: qsTr("Sign in with ChatGPT…")
                                onClicked: page.signInCodex(false)
                            }
                            Button {
                                objectName: "aiCodexImport"
                                visible: page.codex.canImport === true
                                text: qsTr("Import from ~/.codex (read-only)")
                                onClicked: page.dialog.ask({
                                    title: qsTr("Import the ChatGPT sign-in"),
                                    text: qsTr("Import the ChatGPT sign-in from ~/.codex? HikariSub and Codex in your terminal will then share one sign-in, and when one of them refreshes it, the other has to sign in again."),
                                    severity: "warning",
                                    choices: [{ id: "import", text: qsTr("Import"), role: "accept" },
                                              { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
                                }, answer => { if (answer === "import") page.ai.importCodex() })
                            }
                        }
                        Note {
                            text: qsTr("HikariSub keeps its own ChatGPT sign-in for Codex, apart from the one in ~/.codex, so neither signs the other out.")
                            Layout.leftMargin: 0
                            Layout.topMargin: 0
                        }
                    }
                    ColumnLayout {
                        objectName: "aiCodexSignedIn"
                        visible: !codexColumn.panel && page.codex.signedIn === true
                        spacing: 6
                        Layout.fillWidth: true
                        RowLayout {
                            spacing: 6
                            Label {
                                objectName: "aiCodexEmail"
                                text: qsTr("Signed in as %1").arg(page.codex.email ?? "")
                                textFormat: Text.PlainText
                            }
                            Button {
                                objectName: "aiCodexSignOut"
                                text: qsTr("Sign out")
                                onClicked: page.ai.signOutCodex()
                            }
                        }
                        Note {
                            text: qsTr("Codex runs with this sign-in. Signing out here leaves Codex in your terminal signed in.")
                            Layout.leftMargin: 0
                            Layout.topMargin: 0
                        }
                    }
                    ColumnLayout {
                        objectName: "aiCodexExpired"
                        visible: !codexColumn.panel && codexColumn.error === "expired"
                        spacing: 6
                        Layout.fillWidth: true
                        RowLayout {
                            spacing: 8
                            Icon { iconRole: "notice-warning" }
                            Label { text: qsTr("Sign-in expired"); color: Theme.warning }
                            Button {
                                objectName: "aiCodexSignInAgain"
                                text: qsTr("Sign in again…")
                                onClicked: page.signInCodex(false)
                            }
                        }
                        Note {
                            text: qsTr("ChatGPT ended HikariSub’s session, for example after a password change or a sign-out on all devices. Sign in again to keep using Codex.")
                            Layout.leftMargin: 0
                            Layout.topMargin: 0
                        }
                    }
                    Rectangle {
                        objectName: "aiCodexPanel"
                        visible: codexColumn.panel
                        color: Theme.panel
                        border.color: Theme.accent
                        radius: 2
                        implicitHeight: panelColumn.implicitHeight + 24
                        Layout.fillWidth: true
                        Timer {
                            id: clock
                            property double now: Date.now()
                            interval: 1000
                            repeat: true
                            running: codexColumn.state === "code"
                            triggeredOnStart: true
                            onTriggered: now = Date.now()
                        }
                        ColumnLayout {
                            id: panelColumn
                            x: 12
                            y: 12
                            width: parent.width - 24
                            spacing: 8
                            Label { text: qsTr("Sign in with ChatGPT"); font.bold: true }
                            Label {
                                visible: codexColumn.state === "code"
                                text: qsTr("Open auth.openai.com, sign in to ChatGPT and enter this code.")
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: codexColumn.state === "code"
                                spacing: 10
                                Label {
                                    objectName: "aiCodexCode"
                                    text: page.codex.userCode ?? ""
                                    textFormat: Text.PlainText
                                    font.family: "monospace"
                                    font.pixelSize: 22
                                    font.bold: true
                                    font.letterSpacing: 2
                                }
                                IconTextButton {
                                    objectName: "aiCodexCopy"
                                    iconRole: "edit-copy"
                                    text: qsTr("Copy")
                                    onClicked: page.dialog.app.copyText(page.codex.userCode)
                                }
                                LinkButton {
                                    objectName: "aiCodexOpen"
                                    iconRole: "open-link"
                                    text: qsTr("Open auth.openai.com")
                                    onClicked: page.ai.openSignInPage()
                                }
                            }
                            Label {
                                visible: codexColumn.state === "browser"
                                text: qsTr("auth.openai.com is open in your browser. Sign in there; HikariSub finishes when the page says you can close it.")
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: codexColumn.state !== ""
                                spacing: 8
                                Layout.fillWidth: true
                                BusyIcon {}
                                Label {
                                    objectName: "aiCodexWaiting"
                                    text: codexColumn.state === "code"
                                          ? qsTr("Waiting for the code · it expires in %1").arg(page.countdown(page.codex.expiresAt, clock.now))
                                          : codexColumn.state === "browser" ? qsTr("Waiting for the browser") : ""
                                    color: Theme.muted
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Button {
                                    objectName: "aiCodexOpenAgain"
                                    visible: codexColumn.state === "browser"
                                    text: qsTr("Open the page again")
                                    onClicked: page.ai.openSignInPage()
                                }
                                Button {
                                    objectName: "aiCodexCancel"
                                    text: qsTr("Cancel")
                                    onClicked: page.ai.cancelSignIn()
                                }
                            }
                            LinkButton {
                                objectName: "aiCodexUseBrowser"
                                iconRole: "open-link"
                                visible: codexColumn.state === "code" && page.codex.browserAvailable === true
                                text: qsTr("Can’t use a code? Sign in through the browser")
                                onClicked: page.signInCodex(true)
                            }
                            Label {
                                objectName: "aiCodexPortBusy"
                                visible: codexColumn.state === "code" && page.codex.browserAvailable === false
                                text: page.signInText("portBusy", "")
                                color: Theme.muted
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: codexColumn.state === "" && codexColumn.error !== ""
                                spacing: 8
                                Layout.fillWidth: true
                                Icon {
                                    readonly property bool warns: codexColumn.error === "codesOff"
                                    iconRole: warns ? "notice-warning" : "notice-error"
                                    Layout.alignment: Qt.AlignTop
                                }
                                Label {
                                    objectName: "aiCodexError"
                                    text: page.signInText(codexColumn.error, page.codex.host || "auth.openai.com")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            RowLayout {
                                visible: codexColumn.state === "" && codexColumn.error !== ""
                                spacing: 8
                                PrimaryButton {
                                    objectName: "aiCodexRetry"
                                    visible: codexColumn.error !== "portBusy"
                                    text: codexColumn.error === "codesOff" ? qsTr("Sign in through the browser")
                                        : codexColumn.error === "codeExpired" ? qsTr("Get a new code") : qsTr("Try again")
                                    onClicked: page.signInCodex(codexColumn.error === "codesOff" || (page.codexBrowser && codexColumn.error !== "codeExpired"))
                                }
                                Button {
                                    objectName: "aiCodexDismiss"
                                    text: qsTr("Cancel")
                                    onClicked: page.ai.cancelSignIn()
                                }
                            }
                        }
                    }
                }
            }
        }

        SettingsSection {
            objectName: "aiPrivacy"
            visible: page.live
            title: qsTr("Privacy")
            AiCheck { setting: "ai.promptCaching"; text: qsTr("Use prompt caching") }
            Note { text: qsTr("The provider keeps the start of a conversation for a few minutes, so the next turn costs less. Nothing is stored on this computer.") }
            LinkButton {
                objectName: "aiWhatIsSent"
                iconRole: "open-link"
                text: qsTr("What is sent to providers")
                Layout.leftMargin: 22
                onClicked: page.ai.openWhatIsSent()
            }
            Note { text: qsTr("Requests go only to the providers you add. The operating system doesn’t sandbox the AI component yet.") }
        }
    }
}
