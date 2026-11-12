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
    readonly property var inlineAi: dialog.inlineAi
    readonly property var skills: dialog.skills
    property int editingAction: -2
    readonly property bool stagedOff: dialog.values["ai.off"] === true
    readonly property bool live: !stagedOff && !!ai && ai.on
    readonly property var providerList: ai ? ai.providers : []
    property string adding: ""
    property string editing: ""
    readonly property var mcpServers: ai ? ai.mcpServers : null
    readonly property var mcpServerList: mcpServers ? mcpServers.servers : []
    property string mcpEditing: ""
    property var stagedRoles: ({})
    readonly property var codex: ai ? ai.codex : ({})
    readonly property var components: ai ? ai.components : ({})
    readonly property var agents: ai ? ai.agents : ({})
    readonly property var acp: ai ? ai.acp : ({ available: false, antigravity: ({}) })
    readonly property bool autoUpdate: dialog.values["ai.autoUpdate"] === true && components.policyOff !== true
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
            ai.checkAgents()
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
    function askDeleteMcpServer(row) {
        dialog.ask({
            title: qsTr("Delete MCP server"),
            text: qsTr("Delete the MCP server “%1”?").arg(row.name),
            detail: qsTr("Its secrets are removed from the keychain, and the agent loses its tools."),
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, answer => { if (answer === "delete") page.mcpServers.remove(row.id) })
    }
    function mcpStatusText(row) {
        switch (row.state) {
        case "connecting": return qsTr("Starting…")
        case "ready": return qsTr("Connected · %n tool(s)", "", row.toolCount)
        case "failed": return row.error !== "" ? qsTr("Failed: %1").arg(row.error) : qsTr("Failed")
        }
        return qsTr("Starts when the agent needs it")
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
        case "downloadSignature": return qsTr("Couldn’t check: bad signature")
        case "downloadDamaged": return qsTr("Downloaded AI component is damaged")
        case "downloadDisk": return qsTr("Can’t save the AI component")
        }
        return qsTr("Test failed")
    }
    function megabytes(bytes) { return Math.max(1, Math.round(bytes / 1000000)) }
    function tenths(bytes) { return Number(bytes / 1000000).toLocaleString(Qt.locale(), "f", 1) }
    function checkedText(ms) {
        const minutes = Math.floor((Date.now() - ms) / 60000)
        if (minutes < 1)
            return qsTr("Up to date · checked just now")
        if (minutes < 60)
            return qsTr("Up to date · checked %n minute(s) ago", "", minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("Up to date · checked %n hour(s) ago", "", hours)
        return qsTr("Up to date · checked %1").arg(new Date(ms).toLocaleString(Qt.locale(), Locale.ShortFormat))
    }
    function componentStatus(c) {
        if (c.state === "checking")
            return { tone: "muted", text: qsTr("Checking…") }
        if (c.state === "downloading")
            return { tone: "muted", text: qsTr("Downloading %1 · %2 of %3 MB").arg(c.available, tenths(c.received), tenths(c.total)) }
        if (c.error === "downloadNetwork" || c.error === "downloadManifest")
            return { tone: "bad", text: qsTr("Couldn’t check: no connection") }
        if (c.error)
            return { tone: "bad", text: failureText(c.error, 0) }
        if (c.staged)
            return { tone: "ok", text: qsTr("%1 is ready · starts once AI has been idle for 2 minutes").arg(c.staged) }
        if (c.available)
            return { tone: "warn", text: qsTr("%1 is available · %2 MB").arg(c.available, tenths(c.availableBytes)) }
        if (c.newerNeedsApp)
            return { tone: "ok", text: qsTr("Newest this HikariSub runs") }
        return { tone: "ok", text: c.checkedAt ? checkedText(c.checkedAt) : qsTr("Up to date") }
    }
    function componentNote(c) {
        if (c.state)
            return ""
        if (c.error === "downloadNetwork" || c.error === "downloadManifest")
            return c.checkedAt ? qsTr("Last checked %1. HikariSub tries again in an hour.").arg(new Date(c.checkedAt).toLocaleString(Qt.locale(), Locale.ShortFormat))
                               : qsTr("HikariSub tries again in an hour.")
        if (c.error === "downloadSignature")
            return qsTr("HikariSub keeps %1 and installs nothing from that list.").arg(c.version)
        if (!c.error && !c.staged && !c.available && c.newerNeedsApp)
            return qsTr("Update HikariSub to get newer AI features.")
        return ""
    }
    function toneColor(tone) {
        return tone === "ok" ? Theme.success : tone === "warn" ? Theme.warning : tone === "bad" ? Theme.danger : Theme.muted
    }
    function methodName(row, name) {
        switch (row.method) {
        case "native": return row.agent === "claude" ? qsTr("Claude’s installer") : qsTr("Codex’s installer")
        case "npm": return "npm"
        case "pnpm": return "pnpm"
        case "bun": return "Bun"
        case "yarn": return "Yarn"
        case "volta": return "Volta"
        case "homebrew": return "Homebrew"
        case "winget": return "WinGet"
        case "chocolatey": return "Chocolatey"
        case "mise": return "mise"
        }
        return ""
    }
    function agentFailed(row) { return row.outcome === "unchanged" || row.outcome === "failed" || row.outcome === "timedOut" }
    function agentNeedsUpdate(row) { return row.badge === "update" || row.badge === "unsupported" || row.badge === "broken" }
    function refusalText(refused) {
        return refused === "sandbox" ? qsTr("Not offered: this system can’t keep it to its own folders.")
             : refused === "project" ? qsTr("Not offered: its command is inside an open project, which it could read.")
             : ""
    }
    function agentLine(row, name) {
        if (!row.found)
            return row.missing ? row.missing : qsTr("Not found on PATH.")
        if (row.acp)
            return row.badge === "unsupported"
                   ? qsTr("%1 is older than the oldest HikariSub supports (%2). Run this in a terminal:").arg(row.version, row.minimum)
                   : row.version ? row.version : qsTr("Found on PATH.")
        if (row.state === "updating")
            return qsTr("Running %1. New sessions use the new version.").arg(row.command)
        if (row.outcome === "unchanged")
            return qsTr("Still %1 after %2. Run it in a terminal to see why.").arg(row.version, row.command)
        if (agentFailed(row))
            return qsTr("%1 didn’t finish. Run it in a terminal to see why.").arg(row.command)
        if (!row.version)
            return qsTr("Found on PATH.")
        if (row.session && row.canUpdate && agentNeedsUpdate(row))
            return qsTr("A %1 session is running in the agent panel. Update now waits until it ends.").arg(name)
        const method = methodName(row, name)
        const how = row.canUpdate ? qsTr("Installed with %1.").arg(method)
                  : row.command ? qsTr("Installed with %1, so HikariSub won’t update it. Run this in a terminal:").arg(method)
                  : method ? qsTr("Installed with %1; update it as you installed it.").arg(method)
                  : qsTr("Installed in an unknown way; update it as you installed it.")
        if (row.badge === "broken")
            return qsTr("%1 has a fault that breaks HikariSub’s agent.").arg(row.version) + " " + how
        if (row.badge === "unsupported")
            return qsTr("%1 is older than the oldest HikariSub supports (%2).").arg(row.version, row.minimum) + " " + how
        if (row.badge === "update")
            return row.canUpdate ? qsTr("%1 · %2 is out · installed with %3").arg(row.version, row.latest, method)
                                 : qsTr("%1 · %2 is out.").arg(row.version, row.latest) + " " + how
        return method ? qsTr("%1 · installed with %2").arg(row.version, method)
                      : qsTr("%1 · installed in an unknown way").arg(row.version)
    }
    function agentBadge(row) {
        if (row.state === "updating")
            return { tone: "muted", text: qsTr("Updating…") }
        if (agentFailed(row))
            return { tone: "bad", text: qsTr("Update didn’t take") }
        switch (row.badge) {
        case "update": return { tone: "warn", text: qsTr("Update available") }
        case "current": return { tone: "ok", text: qsTr("Up to date") }
        case "unsupported": return { tone: "warn", text: qsTr("Unsupported version") }
        case "broken": return { tone: "bad", text: qsTr("Known broken version") }
        }
        return { tone: "", text: "" }
    }

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
        property bool forcedOff: false
        function refresh() { checked = !forcedOff && page.dialog.values[setting] === true }
        onForcedOffChanged: refresh()
        Component.onCompleted: {
            if (contentItem && contentItem.wrapMode !== undefined)
                contentItem.wrapMode = Text.Wrap
            refresh()
        }
        property bool asksFirst: false
        signal askedOn()
        Connections { target: page.dialog; function onReloaded() { check.refresh() } }
        onToggled: {
            if (asksFirst && checked) {
                checked = false
                askedOn()
            } else {
                page.dialog.put(setting, checked)
            }
        }
    }
    component SkillRow: RowLayout {
        id: skillRow
        required property var modelData
        readonly property bool hidden: modelData.hidden !== ""
        readonly property bool broken: modelData.problem !== ""
        readonly property bool capped: modelData.files < modelData.found
        objectName: "aiSkill_" + modelData.origin + "_" + modelData.name
        spacing: 8
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        Icon {
            iconRole: "notice-warning"
            opacity: skillRow.broken ? 1 : 0
            Accessible.ignored: true
        }
        Label {
            text: skillRow.modelData.name
            font.bold: !skillRow.hidden
            font.strikeout: skillRow.hidden
            color: skillRow.hidden ? Theme.disabled : Theme.text
            elide: Text.ElideRight
            Layout.preferredWidth: 150
        }
        TierChip { id: chip; origin: skillRow.modelData.origin; enabled: !skillRow.hidden }
        Label {
            id: detail
            objectName: "aiSkillDetail"
            text: skillRow.broken ? skillRow.modelData.problem
                : skillRow.hidden ? skillRow.modelData.hidden
                : skillRow.capped ? qsTr("Only the first %1 of its %2 files are offered.").arg(skillRow.modelData.files).arg(skillRow.modelData.found)
                : skillRow.modelData.description
            color: skillRow.broken ? Theme.danger : skillRow.capped && !skillRow.hidden ? Theme.warning : Theme.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            visible: !skillRow.hidden && !skillRow.broken
            text: qsTr("%n file(s)", "", skillRow.modelData.files)
            color: Theme.muted
        }
        Accessible.role: Accessible.ListItem
        Accessible.name: skillRow.modelData.name
        Accessible.description: [chip.text, detail.text].join(" · ")
    }

    component TierChip: Rectangle {
        id: chipItem
        property string origin
        readonly property string text: origin === "project" ? qsTr("Project") : origin === "mine" ? qsTr("Mine") : qsTr("Built in")
        implicitWidth: 86
        implicitHeight: 20
        radius: 10
        color: "transparent"
        border.color: !enabled ? Theme.disabled : origin === "project" ? Theme.accent : Theme.line
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 7
            spacing: 4
            Icon {
                iconRole: chipItem.origin === "project" ? "project" : chipItem.origin === "mine" ? "mine" : "builtin-script"
                size: 14
                enabled: chipItem.enabled
            }
            Label {
                text: chipItem.text
                font.pixelSize: 11
                color: !chipItem.enabled ? Theme.disabled : chipItem.origin === "project" ? Theme.accent : Theme.text
                Layout.fillWidth: true
            }
        }
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
    component Badge: Rectangle {
        id: badge
        property string tone
        property alias text: badgeLabel.text
        visible: text !== ""
        color: "transparent"
        border.color: page.toneColor(tone)
        radius: height / 2
        implicitWidth: badgeLabel.implicitWidth + 16
        implicitHeight: badgeLabel.implicitHeight + 2
        Label {
            id: badgeLabel
            anchors.centerIn: parent
            color: page.toneColor(badge.tone)
            font.pixelSize: 11
        }
    }
    component Command: RowLayout {
        id: command
        property string text
        spacing: 8
        Layout.fillWidth: true
        Label {
            text: command.text
            textFormat: Text.PlainText
            font.family: page.codeFontFamily
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        IconTextButton {
            iconRole: "edit-copy"
            text: qsTr("Copy")
            onClicked: page.dialog.app.copyText(command.text)
        }
    }
    component AgentProgram: ColumnLayout {
        id: program
        required property string agent
        required property string name
        readonly property var row: Object.assign({ agent: agent }, page.agents[agent] ?? {})
        readonly property var badge: page.agentBadge(row)
        readonly property bool failing: page.agentFailed(row)
        readonly property bool updating: row.state === "updating"
        readonly property bool offersUpdate: row.canUpdate === true && !updating && !failing && page.agentNeedsUpdate(row)
        objectName: "aiAgent_" + agent
        spacing: 6
        Layout.fillWidth: true
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Label { text: program.name; font.bold: true }
            Label {
                text: program.row.path ?? ""
                textFormat: Text.PlainText
                font.family: page.codeFontFamily
                color: Theme.muted
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Badge {
                objectName: "aiAgentBadge_" + program.agent
                tone: program.badge.tone
                text: program.badge.text
            }
        }
        Label {
            objectName: "aiAgentRefused_" + program.agent
            visible: text.length > 0
            text: page.refusalText(program.row.refused ?? "")
            color: Theme.warning
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "aiAgentLine_" + program.agent
            text: page.agentLine(program.row, program.name)
            textFormat: Text.PlainText
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: program.failing && (program.row.lastLine ?? "") !== ""
            text: program.row.lastLine ?? ""
            textFormat: Text.PlainText
            font.family: page.codeFontFamily
            color: Theme.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Command {
            objectName: "aiAgentCommand_" + program.agent
            visible: program.row.state !== "updating" && (program.row.command ?? "") !== ""
                     && (program.failing || (!program.row.canUpdate && page.agentNeedsUpdate(program.row)))
            text: program.row.command ?? ""
        }
        RowLayout {
            visible: program.offersUpdate || program.updating
            spacing: 8
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "aiAgentUpdate_" + program.agent
                visible: program.offersUpdate
                enabled: program.row.session !== true
                text: qsTr("Update now")
                onClicked: page.ai.updateAgent(program.agent)
            }
            Button {
                objectName: "aiAgentCancel_" + program.agent
                visible: program.updating
                text: qsTr("Cancel")
                onClicked: page.ai.cancelAgentUpdate(program.agent)
            }
        }
    }
    component CustomAgentForm: GridLayout {
        id: agentForm
        required property var agent
        required property Item list
        objectName: "aiCustomAgentForm"
        columns: 2
        columnSpacing: 10
        rowSpacing: 6
        Label { text: qsTr("Name") }
        TextField {
            id: agentName
            objectName: "aiCustomAgentName"
            text: agentForm.agent.name
            Accessible.name: qsTr("Name")
            Layout.fillWidth: true
        }
        Label { text: qsTr("Command") }
        TextField {
            id: agentCommand
            objectName: "aiCustomAgentCommand"
            text: agentForm.agent.command
            placeholderText: "my-agent"
            font.family: page.codeFontFamily
            Accessible.name: qsTr("Command")
            Layout.fillWidth: true
        }
        Label { text: qsTr("Arguments") }
        TextField {
            id: agentArguments
            objectName: "aiCustomAgentArguments"
            text: agentForm.agent.arguments
            placeholderText: "--acp"
            font.family: page.codeFontFamily
            Accessible.name: qsTr("Arguments")
            Layout.fillWidth: true
        }
        RowLayout {
            spacing: 8
            Layout.columnSpan: 2
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                onClicked: agentForm.list.editing = ""
            }
            PrimaryButton {
                objectName: "aiCustomAgentSave"
                enabled: agentName.text.trim() !== "" && agentCommand.text.trim() !== ""
                text: qsTr("Save")
                onClicked: agentForm.list.save(agentForm.agent.id || page.acp.newId(), agentName.text, agentCommand.text,
                                             agentArguments.text)
            }
        }
    }
    component DownloadNotice: Rectangle {
        id: notice
        property string title
        property string text
        default property alias actions: noticeActions.data
        readonly property var download: page.ai ? page.ai.download : ({})
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
                    text: notice.title
                    font.bold: true
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: notice.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: !!notice.download.active
                    spacing: 8
                    ProgressBar {
                        objectName: notice.objectName + "Progress"
                        from: 0
                        to: Math.max(1, notice.download.size || 0)
                        value: notice.download.received || 0
                        Layout.preferredWidth: 180
                    }
                    Label {
                        text: qsTr("%1 of %2 MB").arg(Math.round((notice.download.received || 0) / 1000000), page.megabytes(notice.download.size || 0))
                        color: Theme.muted
                    }
                }
                RowLayout {
                    id: noticeActions
                    visible: children.length > 0
                    spacing: 8
                }
            }
        }
    }
    component StatusDot: Rectangle {
        property string status
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: status === "ok" ? Theme.success : status === "failed" ? Theme.danger : Theme.muted
    }

    component McpServerForm: Rectangle {
        id: serverForm
        property var existing: ({})
        readonly property bool editingOne: existing.id !== undefined
        property string error: ""
        objectName: "aiMcpServerForm"
        color: Theme.panel
        border.color: Theme.accent
        border.width: 1
        radius: 2
        implicitHeight: serverColumn.implicitHeight + 24
        Layout.fillWidth: true
        function close() { page.mcpEditing = "" }
        Component.onCompleted: {
            serverName.text = existing.name ?? ""
            serverKind.currentIndex = existing.transport === "http" ? 1 : 0
            serverCommand.text = existing.command ?? ""
            serverUrl.text = existing.url ?? ""
            serverTrusted.checked = existing.trusted === true
            serverName.forceActiveFocus()
        }
        Connections {
            target: page.mcpServers
            function onSaved(id, error) {
                if (error === "")
                    serverForm.close()
                else
                    serverForm.error = error
            }
        }
        ColumnLayout {
            id: serverColumn
            x: 12
            y: 12
            width: serverForm.width - 24
            spacing: 8
            Label {
                text: serverForm.editingOne ? qsTr("Edit “%1”").arg(serverForm.existing.name) : qsTr("Add an MCP server")
                font.bold: true
                Layout.fillWidth: true
            }
            SettingsRow {
                text: qsTr("Name")
                labelWidth: 150
                TextField { id: serverName; objectName: "aiMcpServerName"; Layout.fillWidth: true; Accessible.name: qsTr("Name") }
            }
            SettingsRow {
                text: qsTr("Runs as")
                labelWidth: 150
                ComboBox {
                    id: serverKind
                    objectName: "aiMcpServerKind"
                    model: [qsTr("A command on this computer"), qsTr("A URL (streamable HTTP)")]
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Runs as")
                }
            }
            SettingsRow {
                visible: serverKind.currentIndex === 0
                text: qsTr("Command")
                labelWidth: 150
                TextField {
                    id: serverCommand
                    objectName: "aiMcpServerCommand"
                    placeholderText: "npx -y @modelcontextprotocol/server-everything"
                    font.family: page.codeFontFamily
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Command")
                }
            }
            SettingsRow {
                visible: serverKind.currentIndex === 0
                text: qsTr("Environment")
                labelWidth: 150
                TextArea {
                    id: serverEnvironment
                    objectName: "aiMcpServerEnvironment"
                    placeholderText: serverForm.editingOne ? qsTr("Leave empty to keep the stored variables") : "API_KEY=…"
                    font.family: page.codeFontFamily
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Environment")
                }
            }
            SettingsRow {
                visible: serverKind.currentIndex === 1
                text: qsTr("URL")
                labelWidth: 150
                TextField {
                    id: serverUrl
                    objectName: "aiMcpServerUrl"
                    placeholderText: "https://example.com/mcp"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("URL")
                }
            }
            SettingsRow {
                visible: serverKind.currentIndex === 1
                text: qsTr("Bearer token")
                labelWidth: 150
                TextField {
                    id: serverBearer
                    objectName: "aiMcpServerBearer"
                    echoMode: TextInput.Password
                    placeholderText: serverForm.editingOne ? qsTr("Leave empty to keep the stored token") : qsTr("Optional")
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Bearer token")
                }
            }
            Note {
                text: serverKind.currentIndex === 0
                      ? qsTr("One NAME=value per line. They go to the keychain, and only to this server.")
                      : qsTr("The token goes to the keychain, and only to this URL.")
            }
            CheckBox {
                id: serverTrusted
                objectName: "aiMcpServerTrusted"
                text: qsTr("Trust what this server says about its tools")
            }
            Note { text: qsTr("A trusted server’s read-only tools run without asking. Without trust, every tool asks as a change would.") }
            Note {
                objectName: "aiMcpServerError"
                visible: serverForm.error !== ""
                color: Theme.danger
                text: {
                    switch (serverForm.error) {
                    case "name": return qsTr("Give the server a name.")
                    case "command": return qsTr("Type the command that starts the server.")
                    case "url": return qsTr("Type the server’s http or https URL.")
                    case "environment": return qsTr("Write each variable as NAME=value on its own line.")
                    }
                    return qsTr("The keychain didn’t take the secrets; nothing was saved.")
                }
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "aiMcpServerCancel"
                    text: qsTr("Cancel")
                    onClicked: serverForm.close()
                }
                PrimaryButton {
                    objectName: "aiMcpServerSave"
                    text: serverForm.editingOne ? qsTr("Save") : qsTr("Add")
                    onClicked: {
                        serverForm.error = ""
                        page.mcpServers.save({ id: serverForm.existing.id ?? "", name: serverName.text,
                                               transport: serverKind.currentIndex === 1 ? "http" : "stdio",
                                               command: serverCommand.text, url: serverUrl.text,
                                               environment: serverEnvironment.text, bearer: serverBearer.text,
                                               trusted: serverTrusted.checked })
                    }
                }
            }
        }
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
            DownloadNotice {
                objectName: "aiDownloadNotice"
                visible: form.download.needed
                title: form.download.size > 0
                       ? qsTr("Your first provider downloads the AI component once: %1 MB.").arg(page.megabytes(form.download.size))
                       : qsTr("Your first provider downloads the AI component once.")
                text: qsTr("Test connection and Add download it. It runs only when you use an AI feature, and never while AI is turned off.")
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
            Timer {
                interval: 2000
                repeat: true
                running: page.live && page.visible
                onTriggered: page.ai.refreshAgentSessions()
            }
            Rectangle {
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: claudeProgram.implicitHeight + 20
                Layout.fillWidth: true
                AgentProgram {
                    id: claudeProgram
                    agent: "claude"
                    name: "Claude Code"
                    x: 10
                    y: 10
                    width: parent.width - 20
                }
            }
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
                    AgentProgram { agent: "codex"; name: "Codex" }
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
            Rectangle {
                visible: page.acp.available
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: openCodeProgram.implicitHeight + 20
                Layout.fillWidth: true
                AgentProgram {
                    id: openCodeProgram
                    agent: "opencode"
                    name: "OpenCode"
                    x: 10
                    y: 10
                    width: parent.width - 20
                }
            }
            Rectangle {
                visible: page.acp.available
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: antigravityColumn.implicitHeight + 20
                Layout.fillWidth: true
                ColumnLayout {
                    id: antigravityColumn
                    objectName: "aiAntigravity"
                    readonly property var sign: page.acp.antigravity
                    readonly property bool found: (page.agents.agy ?? {}).found === true
                    x: 10
                    y: 10
                    width: parent.width - 20
                    spacing: 6
                    AgentProgram { agent: "agy"; name: "Antigravity" }
                    RowLayout {
                        objectName: "aiAntigravitySignedOut"
                        visible: antigravityColumn.found && antigravityColumn.sign.state === "" && !antigravityColumn.sign.signedIn
                        spacing: 8
                        Label { text: qsTr("Not signed in") }
                        Button {
                            objectName: "aiAntigravitySignIn"
                            text: qsTr("Sign in with Google…")
                            onClicked: page.acp.signIn()
                        }
                    }
                    RowLayout {
                        objectName: "aiAntigravitySignedIn"
                        visible: antigravityColumn.sign.state === "" && antigravityColumn.sign.signedIn === true
                        spacing: 6
                        Label { text: qsTr("Signed in with Google") }
                        Button {
                            objectName: "aiAntigravitySignOut"
                            text: qsTr("Sign out")
                            onClicked: page.acp.signOut()
                        }
                    }
                    ColumnLayout {
                        objectName: "aiAntigravityPanel"
                        visible: antigravityColumn.sign.state !== ""
                        spacing: 6
                        Layout.fillWidth: true
                        Label {
                            text: antigravityColumn.sign.state === "code"
                                  ? qsTr("Google’s sign-in page is open in your browser. Sign in there, then paste the code the page shows.")
                                  : antigravityColumn.sign.state === "checking"
                                    ? qsTr("Checking the code…")
                                    : qsTr("Starting Antigravity’s sign-in…")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            visible: antigravityColumn.sign.state === "code"
                            spacing: 8
                            Layout.fillWidth: true
                            TextField {
                                id: antigravityCode
                                objectName: "aiAntigravityCode"
                                placeholderText: qsTr("Code from Google’s page")
                                Accessible.name: placeholderText
                                Layout.fillWidth: true
                                onAccepted: antigravityContinue.clicked()
                            }
                            PrimaryButton {
                                id: antigravityContinue
                                objectName: "aiAntigravityContinue"
                                enabled: antigravityCode.text.trim() !== ""
                                text: qsTr("Continue")
                                onClicked: {
                                    page.acp.enterCode(antigravityCode.text)
                                    antigravityCode.clear()
                                }
                            }
                        }
                        RowLayout {
                            spacing: 8
                            BusyIcon {}
                            Item { Layout.fillWidth: true }
                            Button {
                                visible: antigravityColumn.sign.state === "code"
                                text: qsTr("Open the page again")
                                onClicked: page.acp.openPage()
                            }
                            Button {
                                objectName: "aiAntigravityCancel"
                                text: qsTr("Cancel")
                                onClicked: page.acp.cancel()
                            }
                        }
                    }
                    RowLayout {
                        visible: (antigravityColumn.sign.error ?? "") !== ""
                        spacing: 8
                        Icon { iconRole: "notice-error"; Layout.alignment: Qt.AlignTop }
                        Label {
                            objectName: "aiAntigravityError"
                            text: antigravityColumn.sign.error ?? ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    Note {
                        visible: antigravityColumn.found
                        text: qsTr("HikariSub keeps its own Google sign-in for Antigravity and never reads ~/.gemini.")
                        Layout.leftMargin: 0
                        Layout.topMargin: 0
                    }
                }
            }
            ColumnLayout {
                id: customAgents
                objectName: "aiCustomAgents"
                visible: page.acp.available
                readonly property var rows: (page.dialog.values["ai.agents.custom"] ?? []).map(entry => {
                    try { return JSON.parse(entry) } catch (e) { return null }
                }).filter(row => row !== null && typeof row.id === "string")
                property string editing: ""
                function save(id, name, command, args) {
                    const row = { id: id, name: name.trim(), command: command.trim(), arguments: args.trim() }
                    const kept = rows.filter(r => r.id !== id)
                    const at = rows.findIndex(r => r.id === id)
                    kept.splice(at < 0 ? kept.length : at, 0, row)
                    page.dialog.put("ai.agents.custom", kept.map(r => JSON.stringify(r)))
                    editing = ""
                }
                function remove(id) {
                    page.dialog.put("ai.agents.custom", rows.filter(r => r.id !== id).map(r => JSON.stringify(r)))
                }
                spacing: 6
                Layout.fillWidth: true
                Repeater {
                    model: customAgents.rows
                    delegate: Rectangle {
                        id: customRow
                        required property var modelData
                        readonly property var found: page.ai ? page.acp.find(modelData.command) : ({})
                        objectName: "aiCustomAgent_" + modelData.id
                        color: "transparent"
                        border.color: Theme.line
                        radius: 2
                        implicitHeight: customColumn.implicitHeight + 20
                        Layout.fillWidth: true
                        ColumnLayout {
                            id: customColumn
                            x: 10
                            y: 10
                            width: parent.width - 20
                            spacing: 6
                            RowLayout {
                                visible: customAgents.editing !== customRow.modelData.id
                                spacing: 8
                                Layout.fillWidth: true
                                Label { text: customRow.modelData.name; textFormat: Text.PlainText; font.bold: true }
                                Label {
                                    text: [customRow.modelData.command, customRow.modelData.arguments].join(" ").trim()
                                    textFormat: Text.PlainText
                                    font.family: page.codeFontFamily
                                    color: Theme.muted
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                IconToolButton {
                                    objectName: "aiCustomAgentEdit"
                                    iconRole: "edit"
                                    text: qsTr("Edit")
                                    onClicked: customAgents.editing = customRow.modelData.id
                                }
                                IconToolButton {
                                    objectName: "aiCustomAgentDelete"
                                    iconRole: "delete"
                                    text: qsTr("Delete")
                                    onClicked: customAgents.remove(customRow.modelData.id)
                                }
                            }
                            Label {
                                visible: customAgents.editing !== customRow.modelData.id
                                objectName: "aiCustomAgentLine"
                                readonly property string refused: page.refusalText(customRow.found.refused ?? "")
                                text: refused.length > 0 ? refused : customRow.found.found ? customRow.found.path
                                                                                           : qsTr("Not found on PATH.")
                                textFormat: Text.PlainText
                                color: refused.length > 0 ? Theme.warning : Theme.muted
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Loader {
                                active: customAgents.editing === customRow.modelData.id
                                visible: active
                                Layout.fillWidth: true
                                sourceComponent: CustomAgentForm { agent: customRow.modelData; list: customAgents }
                            }
                        }
                    }
                }
                Loader {
                    active: customAgents.editing === "new"
                    visible: active
                    Layout.fillWidth: true
                    sourceComponent: CustomAgentForm { agent: ({ id: "", name: "", command: "", arguments: "" }); list: customAgents }
                }
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    IconTextButton {
                        objectName: "aiAddCustomAgent"
                        iconRole: "add"
                        text: qsTr("Add agent")
                        enabled: customAgents.editing === ""
                        onClicked: customAgents.editing = "new"
                    }
                    Item { Layout.fillWidth: true }
                }
                Note {
                    text: qsTr("Any agent that speaks ACP over stdio. It runs with its own folder as its home and can write nowhere else.")
                    Layout.leftMargin: 0
                    Layout.topMargin: 0
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
            property bool offering: false
            property string stayedOff: ""
            function turnOn() {
                page.dialog.put("ai.mcp.server", true)
                mcpServerCheck.checked = true
            }
            function copy(kind) {
                page.dialog.app.copyText(setup[kind])
                copied = kind
                copiedTimer.restart()
            }
            visible: page.live
            title: qsTr("MCP")
            Connections {
                target: page.dialog
                function onReloaded() {
                    if (mcpSection.offering)
                        page.ai.cancelComponent()
                    mcpSection.offering = false
                    mcpSection.stayedOff = ""
                }
            }
            Connections {
                target: page.ai
                function onComponentReady(installed) {
                    if (!mcpSection.offering)
                        return
                    mcpSection.offering = false
                    if (installed)
                        mcpSection.turnOn()
                    else
                        mcpSection.stayedOff = page.failureText(page.ai.download.error, 0, "")
                }
            }
            AiCheck {
                id: mcpServerCheck
                setting: "ai.mcp.server"
                asksFirst: true
                text: qsTr("Let agents outside HikariSub edit through MCP")
                onAskedOn: {
                    mcpSection.stayedOff = ""
                    if (page.ai.componentNeeded())
                        mcpSection.offering = true
                    else
                        mcpSection.turnOn()
                }
            }
            Note { text: qsTr("Claude Code, Codex and other agents read the open documents. Their edits wait in the review until you keep or undo them.") }
            DownloadNotice {
                objectName: "aiMcpDownload"
                visible: mcpSection.offering
                title: download.size > 0
                       ? qsTr("MCP needs the AI component, downloaded once: %1 MB.").arg(page.megabytes(download.size))
                       : qsTr("MCP needs the AI component, downloaded once.")
                text: qsTr("It runs only while an agent uses HikariSub, and never while AI is turned off.")
                PrimaryButton {
                    objectName: "aiMcpDownloadStart"
                    text: qsTr("Download")
                    enabled: !page.ai.download.active
                    onClicked: page.ai.installComponent()
                }
                Button {
                    objectName: "aiMcpDownloadCancel"
                    text: qsTr("Cancel")
                    onClicked: {
                        page.ai.cancelComponent()
                        mcpSection.offering = false
                        mcpSection.stayedOff = qsTr("It needs the AI component")
                    }
                }
            }
            Note {
                objectName: "aiMcpStayedOff"
                visible: mcpSection.stayedOff !== "" && !mcpSection.serverOn && !mcpSection.offering
                color: Theme.danger
                text: qsTr("MCP stays off. %1.").arg(mcpSection.stayedOff)
            }
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
                IconTextButton {
                    objectName: "aiMcpResetToken"
                    iconRole: "refresh"
                    text: qsTr("Reset token")
                    onClicked: {
                        page.ai.mcp.resetToken()
                        mcpSection.copied = "token"
                        copiedTimer.restart()
                    }
                }
                Label {
                    objectName: "aiMcpCopied"
                    visible: mcpSection.copied !== ""
                    text: mcpSection.copied === "token" ? qsTr("New token saved.")
                        : mcpSection.copied === "claudeCode" ? qsTr("Copied. Run it in a terminal.")
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
                        reason: qsTr("Holds your token; keep it private.")
                        onTriggered: mcpSection.copy("json")
                    }
                }
            }
            Note {
                objectName: "aiMcpResetNote"
                visible: mcpSection.serverOn
                text: qsTr("Reset token makes a new token. Agents set up with the old one stop working until you copy the setup again.")
            }
            Note {
                objectName: "aiMcpJsonNote"
                visible: mcpSection.serverOn && !mcpSection.setup.json
                text: mcpSection.httpOn ? qsTr("The JSON setup needs the address: press OK or Apply to start listening on 127.0.0.1.")
                                        : qsTr("The JSON setup is for agents that connect over HTTP, so it needs “Also listen on http://127.0.0.1”.")
            }
            SettingsRow {
                objectName: "aiMcpServers"
                visible: !!page.mcpServers
                text: qsTr("MCP servers")
                Layout.topMargin: 8
                Label {
                    objectName: "aiNoMcpServers"
                    visible: page.mcpServerList.length === 0 && page.mcpEditing === ""
                    text: qsTr("No MCP servers yet. Add one to give the agent its tools.")
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Item { visible: page.mcpServerList.length > 0 || page.mcpEditing !== ""; Layout.fillWidth: true }
                IconTextButton {
                    objectName: "aiAddMcpServer"
                    iconRole: "add"
                    text: qsTr("Add server…")
                    enabled: page.mcpEditing === ""
                    onClicked: page.mcpEditing = "new"
                }
            }
            Repeater {
                model: page.mcpServerList
                delegate: ColumnLayout {
                    id: serverRow
                    required property var modelData
                    objectName: "aiMcpServer_" + modelData.id
                    spacing: 2
                    Layout.fillWidth: true
                    Loader {
                        active: page.mcpEditing === serverRow.modelData.id
                        visible: active
                        Layout.fillWidth: true
                        sourceComponent: McpServerForm { existing: serverRow.modelData }
                    }
                    RowLayout {
                        visible: page.mcpEditing !== serverRow.modelData.id
                        spacing: 8
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Label { text: serverRow.modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                text: serverRow.modelData.transport === "http" ? serverRow.modelData.url : serverRow.modelData.command
                                font.family: page.codeFontFamily
                                color: Theme.muted
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        StatusDot {
                            status: serverRow.modelData.state === "ready" ? "ok" : serverRow.modelData.state === "failed" ? "failed" : ""
                        }
                        Label {
                            objectName: "aiMcpServerStatus"
                            text: page.mcpStatusText(serverRow.modelData)
                            color: serverRow.modelData.state === "failed" ? Theme.danger : Theme.muted
                            elide: Text.ElideRight
                            Layout.maximumWidth: 260
                        }
                        IconToolButton {
                            objectName: "aiMcpServerEdit"
                            iconRole: "edit"
                            text: qsTr("Edit")
                            enabled: page.mcpEditing === ""
                            onClicked: page.mcpEditing = serverRow.modelData.id
                        }
                        IconToolButton {
                            objectName: "aiMcpServerDelete"
                            iconRole: "delete"
                            text: qsTr("Delete")
                            onClicked: page.askDeleteMcpServer(serverRow.modelData)
                        }
                    }
                    CheckBox {
                        objectName: "aiMcpServerTrust"
                        visible: page.mcpEditing !== serverRow.modelData.id
                        checked: serverRow.modelData.trusted
                        text: qsTr("Trust what it says about its tools")
                        Layout.leftMargin: 12
                        onToggled: page.mcpServers.setTrusted(serverRow.modelData.id, checked)
                    }
                    Repeater {
                        model: page.mcpEditing !== serverRow.modelData.id ? serverRow.modelData.tools : []
                        delegate: Loader {
                            id: toolRow
                            required property var modelData
                            readonly property bool approvable: !modelData.readOnly && !modelData.alwaysAsks
                            objectName: "aiMcpTool_" + modelData.name
                            Layout.leftMargin: 36
                            sourceComponent: approvable ? approveTool : toolNote
                            Component {
                                id: approveTool
                                CheckBox {
                                    text: qsTr("Run %1 without asking").arg(toolRow.modelData.name)
                                    checked: serverRow.modelData.approved.indexOf(toolRow.modelData.name) >= 0
                                    onToggled: page.mcpServers.setApproved(serverRow.modelData.id, toolRow.modelData.name, checked)
                                }
                            }
                            Component {
                                id: toolNote
                                Label {
                                    color: Theme.muted
                                    text: toolRow.modelData.readOnly ? qsTr("%1 · only reads, never asks").arg(toolRow.modelData.name)
                                                                     : qsTr("%1 · asks every time").arg(toolRow.modelData.name)
                                }
                            }
                        }
                    }
                }
            }
            Loader {
                active: page.mcpEditing === "new"
                visible: active
                Layout.fillWidth: true
                sourceComponent: McpServerForm {}
            }
            Note {
                visible: !!page.mcpServers
                text: qsTr("Servers run as you, outside the AI component’s sandbox. Tools that may change or delete things ask first in every mode. Changes apply at once.")
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
            AiCheck { setting: "ai.agent.autoCompact"; text: qsTr("Compact long conversations automatically") }
            Note { text: qsTr("At 90% of the model’s context window, old tool results are cleared, then older turns are summarised. Your history stays in the panel.") }
        }

        SettingsSection {
            id: skillsSection
            objectName: "aiSkills"
            visible: page.live && !!page.skills
            title: qsTr("Skills")
            property var groups: []
            function read() { groups = page.skills ? page.skills.groups(page.dialog.shellWindow) : [] }
            Component.onCompleted: read()
            onVisibleChanged: if (visible) read()
            readonly property int broken: groups.reduce((n, g) => n + g.rows.filter(r => r.problem !== "").length, 0)
            trailing: LinkButton {
                objectName: "aiSkillsGuide"
                iconRole: "help"
                text: qsTr("About skills")
                onClicked: page.skills.openGuide()
            }
            Rectangle {
                color: "transparent"
                border.color: Theme.line
                radius: 2
                implicitHeight: skillColumn.implicitHeight + 2
                Layout.fillWidth: true
                ColumnLayout {
                    id: skillColumn
                    x: 1
                    y: 1
                    width: parent.width - 2
                    spacing: 0
                    Repeater {
                        model: skillsSection.groups
                        delegate: ColumnLayout {
                            id: group
                            required property var modelData
                            required property int index
                            objectName: "aiSkillGroup_" + modelData.group
                            spacing: 0
                            Layout.fillWidth: true
                            Rectangle { visible: group.index > 0; color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
                            Rectangle {
                                color: Theme.raised
                                implicitHeight: groupHeader.implicitHeight + 8
                                Layout.fillWidth: true
                                RowLayout {
                                    id: groupHeader
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    Label {
                                        text: group.modelData.group === "project" ? qsTr("This project")
                                            : group.modelData.group === "mine" ? qsTr("Mine") : qsTr("Built in")
                                        font.bold: true
                                        font.pixelSize: 11
                                        color: Theme.muted
                                    }
                                    Label {
                                        text: group.modelData.folder
                                        font.family: page.codeFontFamily
                                        font.pixelSize: 11
                                        color: Theme.muted
                                        elide: Text.ElideMiddle
                                        horizontalAlignment: Text.AlignRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                            Label {
                                objectName: "aiSkillsEmpty_" + group.modelData.group
                                visible: group.modelData.rows.length === 0
                                text: group.modelData.group === "project"
                                      ? qsTr("No skills in this project yet. A skill is a folder with a SKILL.md in it.")
                                      : qsTr("No skills of yours yet. A skill is a folder with a SKILL.md in it.")
                                color: Theme.muted
                                wrapMode: Text.Wrap
                                leftPadding: 34
                                topPadding: 6
                                bottomPadding: 6
                                Layout.fillWidth: true
                            }
                            Repeater {
                                model: group.modelData.rows
                                delegate: SkillRow {}
                            }
                        }
                    }
                }
            }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                IconTextButton {
                    objectName: "aiSkillsOpenFolder"
                    iconRole: "folder-open"
                    text: qsTr("Open folder")
                    onClicked: if (page.skills.openFolder()) skillsSection.read()
                }
                IconTextButton {
                    objectName: "aiSkillsOpenProjectFolder"
                    visible: page.dialog.hasProject
                    iconRole: "project"
                    text: qsTr("Open project folder")
                    onClicked: if (page.skills.openProjectFolder(page.dialog.shellWindow)) skillsSection.read()
                }
                IconToolButton {
                    objectName: "aiSkillsRefresh"
                    iconRole: "refresh"
                    text: qsTr("Read the folders again")
                    onClicked: skillsSection.read()
                }
                Item { Layout.fillWidth: true }
            }
            Label {
                objectName: "aiSkillsNote"
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                text: skillsSection.broken > 0
                      ? qsTr("%n skill(s) can’t be offered. Fix them in their folders, then read the folders again.", "", skillsSection.broken)
                      : page.dialog.hasProject
                        ? qsTr("Threads started in this window use this project’s skills. A new or changed skill shows in the next thread.")
                        : qsTr("Hikari AI reads a skill when the work matches its description. Skills are instructions and give no permission. A new or changed skill shows in the next thread.")
            }
        }

        SettingsSection {
            id: inlineSection
            objectName: "aiInline"
            visible: page.live && !!page.inlineAi
            title: qsTr("Inline actions")
            readonly property var actions: page.inlineAi ? page.inlineAi.actions : []
            readonly property var roles: [{id: "chat", name: qsTr("Chat")}, {id: "translator", name: qsTr("Translator")},
                                          {id: "reviewer", name: qsTr("Reviewer")}]
            function save(list) { page.inlineAi.setActions(list) }
            function moved(index, by) {
                const list = actions.slice()
                const [row] = list.splice(index, 1)
                list.splice(index + by, 0, row)
                save(list)
            }
            function edit(index) {
                const row = index >= 0 ? actions[index] : {name: "", instruction: "", role: "chat", ask: false}
                actionName.text = row.name
                actionInstruction.text = row.instruction
                actionRole.currentIndex = Math.max(0, roles.findIndex(r => r.id === row.role))
                actionAsk.checked = !!row.ask
                page.editingAction = index
                actionName.forceActiveFocus()
            }
            function commit() {
                const row = {name: actionName.text.trim(), instruction: actionInstruction.text.trim(),
                             role: roles[actionRole.currentIndex].id, ask: actionAsk.checked}
                const list = actions.slice()
                if (page.editingAction >= 0)
                    list[page.editingAction] = row
                else
                    list.push(row)
                save(list)
                page.editingAction = -2
            }
            Note {
                text: qsTr("Your actions appear in the AI menus. They follow the mode for new threads and change only the text.")
                Layout.leftMargin: 0
                Layout.topMargin: 0
            }
            Repeater {
                model: inlineSection.actions
                delegate: RowLayout {
                    id: actionRow
                    required property var modelData
                    required property int index
                    objectName: "aiInlineAction" + index
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: actionRow.modelData.name
                        ToolTip.text: actionRow.modelData.instruction
                        ToolTip.visible: actionHover.hovered && actionRow.modelData.instruction !== ""
                        HoverHandler { id: actionHover }
                    }
                    Label {
                        color: Theme.muted
                        text: (inlineSection.roles.find(r => r.id === actionRow.modelData.role) ?? inlineSection.roles[0]).name
                              + (actionRow.modelData.ask ? " · " + qsTr("Asks each time") : "")
                    }
                    IconToolButton {
                        iconRole: "move-up"
                        text: qsTr("Move up")
                        enabled: actionRow.index > 0
                        onClicked: inlineSection.moved(actionRow.index, -1)
                    }
                    IconToolButton {
                        iconRole: "move-down"
                        text: qsTr("Move down")
                        enabled: actionRow.index < inlineSection.actions.length - 1
                        onClicked: inlineSection.moved(actionRow.index, 1)
                    }
                    IconToolButton {
                        iconRole: "edit"
                        text: qsTr("Edit")
                        onClicked: inlineSection.edit(actionRow.index)
                    }
                    IconToolButton {
                        iconRole: "remove"
                        text: qsTr("Delete")
                        onClicked: {
                            const list = inlineSection.actions.slice()
                            list.splice(actionRow.index, 1)
                            inlineSection.save(list)
                        }
                    }
                }
            }
            IconTextButton {
                objectName: "aiInlineAdd"
                visible: page.editingAction === -2
                iconRole: "add"
                text: qsTr("Add an action")
                onClicked: inlineSection.edit(-1)
            }
            ColumnLayout {
                objectName: "aiInlineEditor"
                visible: page.editingAction !== -2
                Layout.fillWidth: true
                spacing: 6
                SettingsRow {
                    text: qsTr("Name")
                    TextField {
                        id: actionName
                        objectName: "aiInlineName"
                        Accessible.name: qsTr("Name")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                    }
                }
                SettingsRow {
                    text: qsTr("Instruction")
                    TextArea {
                        id: actionInstruction
                        objectName: "aiInlineInstruction"
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("For example: make it sound like a pirate")
                        Accessible.name: qsTr("Instruction")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                        Layout.preferredHeight: 60
                    }
                }
                SettingsRow {
                    text: qsTr("Model")
                    ComboBox {
                        id: actionRole
                        objectName: "aiInlineRole"
                        model: inlineSection.roles.map(r => r.name)
                        Accessible.name: qsTr("Model")
                        Layout.preferredWidth: 200
                    }
                }
                CheckBox {
                    id: actionAsk
                    objectName: "aiInlineAsk"
                    text: qsTr("Ask each time, starting from this instruction")
                }
                RowLayout {
                    spacing: 8
                    Button {
                        text: qsTr("Cancel")
                        onClicked: page.editingAction = -2
                    }
                    PrimaryButton {
                        objectName: "aiInlineSave"
                        text: qsTr("Save action")
                        enabled: actionName.text.trim() !== "" && (actionAsk.checked || actionInstruction.text.trim() !== "")
                        onClicked: inlineSection.commit()
                    }
                }
            }
            SettingsRow {
                text: qsTr("Translate to")
                TextField {
                    id: translateTo
                    objectName: "setting_ai.inline.translateTo"
                    Accessible.name: qsTr("Translate to")
                    Layout.preferredWidth: 200
                    function refresh() { text = page.dialog.values["ai.inline.translateTo"] ?? "" }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { translateTo.refresh() } }
                    onTextEdited: page.dialog.put("ai.inline.translateTo", text)
                }
            }
            SettingsRow {
                text: qsTr("Lines of context")
                SpinBox {
                    id: contextLines
                    objectName: "setting_ai.inline.contextLines"
                    from: 0
                    to: 5
                    Accessible.name: qsTr("Lines of context")
                    function refresh() { value = page.dialog.values["ai.inline.contextLines"] ?? 2 }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { contextLines.refresh() } }
                    onValueModified: page.dialog.put("ai.inline.contextLines", value)
                }
                Label {
                    color: Theme.muted
                    text: qsTr("before and after each line")
                }
            }
            Note { text: qsTr("Actions save at once. The other settings here save with OK or Apply.") }
        }

        SettingsSection {
            id: componentsSection
            objectName: "aiComponents"
            readonly property var c: page.components
            readonly property var status: page.componentStatus(c)
            readonly property var rollback: c.rollback ?? ({})
            visible: page.live && c.present === true
            title: qsTr("AI components")
            trailing: LinkButton {
                objectName: "aiWhatsNew"
                iconRole: "open-link"
                text: qsTr("What’s new")
                onClicked: page.ai.openWhatsNew()
            }
            Rectangle {
                objectName: "aiRollbackNotice"
                visible: (componentsSection.rollback.version ?? "") !== ""
                color: "transparent"
                border.color: Theme.warning
                radius: 2
                implicitHeight: noticeRow.implicitHeight + 20
                Layout.fillWidth: true
                RowLayout {
                    id: noticeRow
                    x: 10
                    y: 10
                    width: parent.width - 20
                    spacing: 8
                    Icon { iconRole: "notice-warning"; Layout.alignment: Qt.AlignTop }
                    Label {
                        objectName: "aiRollbackText"
                        readonly property var r: componentsSection.rollback
                        text: qsTr("AI component %1 stopped working, so HikariSub went back to %2 and won’t install %1 again.").arg(r.version ?? "", r.to ?? "")
                              + (r.reportSent === true ? " " + qsTr("A crash report was sent.") : "")
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Button {
                        objectName: "aiRollbackDismiss"
                        text: qsTr("Dismiss")
                        onClicked: page.ai.dismissComponentNotice()
                    }
                }
            }
            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 8
                Layout.fillWidth: true
                Label { text: qsTr("AI component"); color: Theme.muted }
                Label { objectName: "aiComponentVersion"; text: componentsSection.c.version ?? ""; textFormat: Text.PlainText }
                Label { visible: !!componentsSection.c.runtime; text: qsTr("Runs on"); color: Theme.muted }
                Label { visible: !!componentsSection.c.runtime; text: "Bun " + componentsSection.c.runtime; textFormat: Text.PlainText }
                Label { visible: libraries.text !== ""; text: qsTr("Provider libraries"); color: Theme.muted }
                Label {
                    id: libraries
                    visible: text !== ""
                    text: [["Anthropic", componentsSection.c.anthropic], ["OpenAI", componentsSection.c.openai],
                           ["Google Gen AI", componentsSection.c.google]].filter(p => !!p[1]).map(p => p[0] + " " + p[1]).join(" · ")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label { text: qsTr("Status"); color: Theme.muted }
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    BusyIcon { visible: !!componentsSection.c.state }
                    Rectangle {
                        visible: !componentsSection.c.state
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: page.toneColor(componentsSection.status.tone)
                    }
                    Label {
                        objectName: "aiComponentStatus"
                        text: componentsSection.status.text
                        textFormat: Text.PlainText
                        color: componentsSection.status.tone === "ok" || componentsSection.status.tone === "muted"
                               ? Theme.text : page.toneColor(componentsSection.status.tone)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    IconTextButton {
                        objectName: "aiComponentCheck"
                        visible: !componentsSection.c.state
                        iconRole: "refresh"
                        text: qsTr("Check now")
                        onClicked: page.ai.checkComponents()
                    }
                    PrimaryButton {
                        objectName: "aiComponentUpdate"
                        visible: !componentsSection.c.state && !componentsSection.c.error && !!componentsSection.c.available
                                 && !componentsSection.c.staged
                        text: qsTr("Update now")
                        onClicked: page.ai.updateComponents()
                    }
                    Button {
                        objectName: "aiComponentCancel"
                        visible: componentsSection.c.state === "downloading"
                        text: qsTr("Cancel")
                        onClicked: page.ai.cancelComponents()
                    }
                }
                Item { visible: componentNote.visible; implicitWidth: 1 }
                Label {
                    id: componentNote
                    objectName: "aiComponentNote"
                    visible: text !== ""
                    text: page.componentNote(componentsSection.c)
                    textFormat: Text.PlainText
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            AiCheck {
                setting: "ai.autoUpdate"
                text: qsTr("Update AI components automatically")
                forcedOff: componentsSection.c.policyOff === true
                enabled: !forcedOff
            }
            Note {
                objectName: "aiAutoUpdateNote"
                text: componentsSection.c.policyOff === true ? qsTr("Your administrator turned automatic updates off on this computer.")
                    : page.autoUpdate ? qsTr("HikariSub checks every hour while AI is on. A new version downloads in the background and starts once AI has been idle for 2 minutes. If it doesn’t start, HikariSub goes back to the one before.")
                    : qsTr("HikariSub still checks every hour, and downloads only when you press Update now.")
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
