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
    readonly property bool webSearchOn: dialog.values["ai.webSearch"] === true
    readonly property string stagedService: dialog.values["ai.searchService"] ?? ""
    readonly property var search: ai ? ai.search : ({})
    readonly property bool searchKeySaved: search.service === stagedService && search.hasKey === true
    readonly property string searchVia: webSearchOn && stagedService !== "" ? serviceName(stagedService) : ""
    property bool replacingSearchKey: false
    property string testedSearchKey: ""
    readonly property var searchServices: [
        {id: "", name: qsTr("None"), menuText: qsTr("None")},
        {id: "brave", name: "Brave Search", menuText: qsTr("Brave Search (search only)")},
        {id: "tavily", name: "Tavily", menuText: "Tavily"},
        {id: "exa", name: "Exa", menuText: "Exa"},
        {id: "firecrawl", name: "Firecrawl", menuText: "Firecrawl"}
    ]
    property bool codexBrowser: false
    readonly property string codeFontFamily: {
        const marked = ["Cascadia Mono", "Consolas", "Menlo", "JetBrains Mono", "Noto Sans Mono"]
        const installed = Qt.fontFamilies()
        return marked.find(family => installed.includes(family)) ?? "monospace"
    }
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
        if (ai && ai.on) {
            ai.checkCodex()
            ai.checkSearchKey(stagedService)
        }
    }
    onStagedServiceChanged: {
        replacingSearchKey = false
        testedSearchKey = ""
        searchKeyField.clear()
        if (ai && ai.on)
            ai.checkSearchKey(stagedService)
    }
    function serviceName(id) {
        return searchServices.find(s => s.id === id)?.name ?? id
    }
    function searchHow(row) {
        if (row.searchesNatively)
            return row.preset === "gemini" ? qsTr("Google Search, and reads pages") : qsTr("Its own search, and reads pages")
        return searchKeySaved ? serviceName(stagedService) : qsTr("Needs a search service")
    }
    function searchStatus() {
        const name = serviceName(search.service ?? "")
        switch (search.state) {
        case "checking": return ""
        case "saving": return qsTr("Saving…")
        case "testing": return qsTr("Testing…")
        case "ok": return qsTr("Connected")
        case "failed":
            switch (search.error) {
            case "unauthorized": return qsTr("Key rejected (%1)").arg(search.httpStatus)
            case "rateLimited": return qsTr("Searches used up (%1)").arg(search.httpStatus)
            case "noProvider": return qsTr("Add a provider to test the key")
            case "network": return qsTr("Can’t reach %1").arg(name)
            case "server": return qsTr("Search service error (%1)").arg(search.httpStatus)
            }
            return failureText(search.error, search.httpStatus)
        }
        return qsTr("Not tested")
    }
    function searchNote() {
        const name = serviceName(stagedService)
        if (!searchKeySaved || replacingSearchKey)
            return qsTr("Saved in the keychain. HikariSub sends only the searches the agent writes, never your files. Test connection runs one search, which counts against your %1 plan.").arg(name)
        if (search.state === "failed" && search.error === "unauthorized")
            return qsTr("%1 refused the key. Paste a new one from your %1 dashboard.").arg(name)
        if (search.state === "failed" && search.error === "rateLimited")
            return qsTr("Your %1 plan has no searches left. Until it resets, providers without their own search answer without searching and say so.").arg(name)
        return qsTr("The key is in the keychain. Providers charge for their own searches on top of tokens. %1 bills searches on your %1 plan.").arg(name)
    }
    function askRemoveSearchKey() {
        const name = serviceName(stagedService)
        dialog.ask({
            title: qsTr("Remove key"),
            text: qsTr("Remove the %1 key?").arg(name),
            detail: qsTr("It is deleted from the keychain. Providers without their own search stop searching until you add a key."),
            severity: "warning",
            choices: [{ id: "remove", text: qsTr("Remove"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, answer => { if (answer === "remove") page.ai.removeSearchKey(page.stagedService) })
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
                        objectName: "aiOpenRouterWaitingText"
                        text: qsTr("Waiting for openrouter.ai in your browser")
                        wrapMode: Text.Wrap
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
                StatusDot {
                    objectName: "aiDraftDot"
                    status: form.draft.state ?? ""
                    visible: draftStatus.text !== ""
                }
                Label {
                    id: draftStatus
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
                IconTextButton {
                    objectName: "aiDraftTest"
                    iconRole: "test-connection"
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
                                              : row.modelData.needsKey && row.modelData.error !== "key" ? qsTr("Key in keychain")
                                                                                                         : qsTr("No key")
                                        color: Theme.muted
                                    }
                                }
                                IconTextButton {
                                    objectName: "aiProviderTest"
                                    iconRole: "test-connection"
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
            objectName: "aiWebSearch"
            visible: page.live
            title: qsTr("Web search")
            AiCheck { setting: "ai.webSearch"; text: qsTr("Let AI search the web") }
            Note { text: qsTr("The agent can look up names, terms, lyrics and references, and says where it found them. The provider or your search service gets each search the agent writes and the pages it opens.") }
            Rectangle {
                objectName: "aiSearchProviders"
                visible: page.webSearchOn && page.providerList.length > 0
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: searchList.implicitHeight + 2
                Layout.fillWidth: true
                Layout.leftMargin: 24
                ColumnLayout {
                    id: searchList
                    x: 1
                    y: 1
                    width: parent.width - 2
                    spacing: 0
                    Repeater {
                        model: page.providerList
                        delegate: RowLayout {
                            id: searchRow
                            required property var modelData
                            readonly property bool needsService: !modelData.searchesNatively && !page.searchKeySaved
                            objectName: "aiSearchHow_" + modelData.id
                            spacing: 8
                            Layout.fillWidth: true
                            Layout.margins: 6
                            Layout.leftMargin: 10
                            Label { text: searchRow.modelData.name; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                objectName: "aiSearchHow"
                                text: page.searchHow(searchRow.modelData)
                                color: searchRow.needsService ? Theme.warning : Theme.success
                            }
                        }
                    }
                }
            }
            Note {
                objectName: "aiGeminiSuggestionsNote"
                visible: page.webSearchOn && page.providerList.some(p => p.preset === "gemini" && p.searchesNatively)
                text: qsTr("Gemini answers that searched show Google’s search suggestions, as Google’s terms require.")
                Layout.topMargin: 0
            }
            SettingsRow {
                id: serviceRow
                text: qsTr("Search service")
                labelWidth: 200
                enabled: page.webSearchOn
                ComboBox {
                    id: serviceChoice
                    objectName: "setting_ai.searchService"
                    readonly property var docsValues: page.searchServices.map(s => ({value: s.id, label: s.menuText}))
                    model: page.searchServices
                    textRole: "menuText"
                    valueRole: "id"
                    Accessible.name: serviceRow.text
                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    function refresh() { currentIndex = Math.max(0, indexOfValue(page.stagedService)) }
                    Component.onCompleted: refresh()
                    Connections { target: page; function onStagedServiceChanged() { serviceChoice.refresh() } }
                    onActivated: index => page.dialog.put("ai.searchService", valueAt(index))
                }
            }
            Note {
                text: page.stagedService === "brave"
                      ? qsTr("Brave Search only searches: providers without their own search can’t read a page with it.")
                      : qsTr("For providers without their own search. Anthropic, Google Gemini, OpenAI and OpenRouter search without one.")
                enabled: page.webSearchOn
                Layout.leftMargin: 208
                Layout.topMargin: -2
            }
            SettingsRow {
                id: searchKeyRow
                objectName: "aiSearchKeyRow"
                visible: page.webSearchOn && page.stagedService !== "" && page.search.state !== "checking"
                text: qsTr("%1 key").arg(page.serviceName(page.stagedService))
                labelWidth: 200
                TextField {
                    id: searchKeyField
                    objectName: "aiSearchKey"
                    visible: !page.searchKeySaved || page.replacingSearchKey
                    echoMode: showSearchKey.checked ? TextInput.Normal : TextInput.Password
                    Layout.fillWidth: true
                    Accessible.name: searchKeyRow.text
                    onAccepted: if (text.trim() !== "") saveSearchKey.clicked()
                    Connections {
                        target: page.ai
                        function onSearchKeyStored() {
                            searchKeyField.clear()
                            page.testedSearchKey = ""
                            page.replacingSearchKey = false
                        }
                    }
                }
                IconToolButton {
                    id: showSearchKey
                    visible: searchKeyField.visible
                    iconRole: checked ? "hide" : "show"
                    text: checked ? qsTr("Hide key") : qsTr("Show key")
                    checkable: true
                }
                StatusDot {
                    visible: searchStatusLabel.text !== ""
                    status: page.search.state ?? ""
                }
                Label {
                    id: searchStatusLabel
                    objectName: "aiSearchStatus"
                    text: page.search.service === page.stagedService ? page.searchStatus() : ""
                    visible: text !== ""
                    color: page.search.state === "ok" ? Theme.success
                         : page.search.state === "failed" ? (page.search.error === "rateLimited" ? Theme.warning : Theme.danger)
                         : Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: !searchKeyField.visible
                }
                IconTextButton {
                    id: saveSearchKey
                    objectName: "aiSearchTest"
                    iconRole: "test-connection"
                    text: qsTr("Test connection")
                    enabled: page.search.state !== "testing" && page.search.state !== "saving"
                             && (searchKeyField.visible ? searchKeyField.text.trim() !== "" : true)
                    onClicked: {
                        if (searchKeyField.visible) {
                            page.testedSearchKey = searchKeyField.text
                            page.ai.saveSearchKey(page.stagedService, searchKeyField.text)
                        } else {
                            page.ai.testSearchKey(page.stagedService)
                        }
                    }
                }
                Button {
                    objectName: "aiSearchAddAnyway"
                    visible: searchKeyField.visible && page.search.service === page.stagedService
                             && page.search.canAddAnyway === true && searchKeyField.text === page.testedSearchKey
                    text: qsTr("Add anyway")
                    onClicked: page.ai.addSearchKeyAnyway(page.stagedService, searchKeyField.text)
                }
                Button {
                    objectName: "aiSearchReplaceKey"
                    visible: page.searchKeySaved && !page.replacingSearchKey && page.search.error === "unauthorized"
                    text: qsTr("Replace key…")
                    onClicked: page.replacingSearchKey = true
                }
                Button {
                    objectName: "aiSearchRemoveKey"
                    visible: page.searchKeySaved && !page.replacingSearchKey
                    text: qsTr("Remove key")
                    onClicked: page.askRemoveSearchKey()
                }
            }
            Note {
                objectName: "aiSearchKeyNote"
                visible: searchKeyRow.visible
                text: page.searchNote()
                Layout.leftMargin: 208
                Layout.topMargin: -2
            }
            LinkButton {
                objectName: "aiSearchKeys"
                visible: searchKeyRow.visible && (!page.searchKeySaved || page.replacingSearchKey)
                iconRole: "open-link"
                text: qsTr("Get a %1 key").arg(page.serviceName(page.stagedService))
                Layout.leftMargin: 206
                onClicked: page.ai.openSearchKeys(page.stagedService)
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
                                    font.family: page.codeFontFamily
                                    font.features: { "zero": 1 }
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
            id: mcpSection
            objectName: "aiMcp"
            readonly property bool serverOn: page.dialog.values["ai.mcp.server"] === true
            readonly property bool httpOn: serverOn && page.dialog.values["ai.mcp.http"] === true
            readonly property var setup: page.ai && page.ai.mcp ? page.ai.mcp.setup : ({})
            property string copied: ""
            function copy(kind) {
                page.dialog.app.copyText(setup[kind])
                copied = kind
                copiedTimer.restart()
            }
            visible: page.live
            title: qsTr("MCP")
            AiCheck { setting: "ai.mcp.server"; text: qsTr("Let agents outside HikariSub edit through MCP") }
            Note { text: qsTr("Claude Code, Codex and other agents read the open documents. Their edits wait in the review until you keep or undo them.") }
            AiCheck {
                property string docsLabel: qsTr("Also listen on http://127.0.0.1")
                setting: "ai.mcp.http"
                visible: mcpSection.serverOn
                Layout.leftMargin: 24
                text: mcpSection.setup.port ? qsTr("Also listen on http://127.0.0.1:%1").arg(mcpSection.setup.port) : docsLabel
            }
            Note {
                visible: mcpSection.serverOn
                Layout.leftMargin: 48
                text: qsTr("For agents that connect over HTTP. Only this computer can reach it, and only with the token.")
            }
            RowLayout {
                visible: mcpSection.serverOn
                spacing: 8
                Layout.leftMargin: 22
                Layout.fillWidth: true
                IconTextButton {
                    id: copySetupButton
                    objectName: "aiMcpCopySetup"
                    iconRole: "edit-copy"
                    text: qsTr("Copy setup")
                    onClicked: copySetupMenu.popup(copySetupButton, 0, copySetupButton.height)
                }
                Label {
                    objectName: "aiMcpCopied"
                    visible: mcpSection.copied !== ""
                    text: mcpSection.copied === "claudeCode" ? qsTr("Copied. Run it in a terminal.")
                                                             : qsTr("Copied. Paste it into the agent’s settings.")
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Timer { id: copiedTimer; interval: 4000; onTriggered: mcpSection.copied = "" }
                }
                ShellMenu {
                    id: copySetupMenu
                    objectName: "aiMcpCopyMenu"
                    ShellMenuItem {
                        objectName: "aiMcpCopyClaudeCode"
                        iconRole: "run-script"
                        text: qsTr("Claude Code command")
                        onTriggered: mcpSection.copy("claudeCode")
                    }
                    ShellMenuItem {
                        objectName: "aiMcpCopyCodex"
                        iconRole: "settings"
                        text: qsTr("Codex config.toml")
                        onTriggered: mcpSection.copy("codex")
                    }
                    ShellMenuItem {
                        objectName: "aiMcpCopyJson"
                        iconRole: "network"
                        enabled: !!mcpSection.setup.json
                        text: qsTr("JSON with the address and token")
                        onTriggered: mcpSection.copy("json")
                    }
                }
            }
            Note {
                objectName: "aiMcpJsonNote"
                visible: mcpSection.serverOn && !mcpSection.setup.json
                text: mcpSection.httpOn ? qsTr("The JSON setup needs the address: press OK or Apply to start listening on 127.0.0.1.")
                                        : qsTr("The JSON setup is for agents that connect over HTTP, so it needs “Also listen on http://127.0.0.1”.")
            }
        }

        SettingsSection {
            objectName: "aiAgent"
            visible: page.live
            title: qsTr("Agent")
            SettingsRow {
                text: qsTr("Mode for new threads")
                ComboBox {
                    id: agentMode
                    objectName: "setting_ai.agent.mode"
                    readonly property var modes: ["manual", "auto", "full"]
                    model: [qsTr("Manual"), qsTr("Auto"), qsTr("Full access")]
                    Accessible.name: qsTr("Mode for new threads")
                    Layout.preferredWidth: 200
                    function refresh() { currentIndex = Math.max(0, modes.indexOf(page.dialog.values["ai.agent.mode"] ?? "auto")) }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { agentMode.refresh() } }
                    onActivated: index => page.dialog.put("ai.agent.mode", modes[index])
                }
            }
            Repeater {
                model: [{setting: "ai.agent.threadCapCents", text: qsTr("Cost cap per thread"), to: 100000},
                        {setting: "ai.agent.dailyCapCents", text: qsTr("Cost cap per day"), to: 1000000}]
                SettingsRow {
                    id: capRow
                    required property var modelData
                    text: modelData.text
                    SpinBox {
                        id: cap
                        objectName: "setting_" + capRow.modelData.setting
                        from: 0
                        to: capRow.modelData.to
                        stepSize: 50
                        editable: true
                        Accessible.name: capRow.modelData.text
                        Layout.preferredWidth: 140
                        textFromValue: (value, locale) => value === 0 ? qsTr("No cap")
                                                                       : "$" + Number(value / 100).toLocaleString(locale, "f", 2)
                        valueFromText: (text, locale) => {
                            const n = Number.fromLocaleString(locale, text.replace("$", "").trim())
                            return isNaN(n) ? 0 : Math.round(n * 100)
                        }
                        validator: RegularExpressionValidator { regularExpression: /.*/ }
                        function refresh() { value = page.dialog.values[capRow.modelData.setting] ?? 0 }
                        Component.onCompleted: refresh()
                        Connections { target: page.dialog; function onReloaded() { cap.refresh() } }
                        onValueModified: page.dialog.put(capRow.modelData.setting, value)
                    }
                }
            }
            Note { text: qsTr("At a cap the turn stops after its current change. The agent never raises a cap; only you can.") }
            AiCheck { setting: "ai.agent.costMeter"; text: qsTr("Show the cost meter") }
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
            Note {
                objectName: "aiSandboxNote"
                color: page.ai.sandboxed ? Theme.muted : Theme.warning
                text: {
                    if (page.searchVia !== "")
                        return page.ai.sandboxed ? qsTr("Requests go only to the providers you add and to %1. The AI component runs in a sandbox and reaches the network only through HikariSub.").arg(page.searchVia)
                                                 : qsTr("Requests go only to the providers you add and to %1. The AI component isn’t sandboxed on this system.").arg(page.searchVia)
                    return page.ai.sandboxed ? qsTr("Requests go only to the providers you add. The AI component runs in a sandbox and reaches the network only through HikariSub.")
                                             : qsTr("Requests go only to the providers you add. The AI component isn’t sandboxed on this system.")
                }
            }
        }
    }
}
