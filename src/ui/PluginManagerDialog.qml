pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Plugins"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "pluginManagerDialog"
    required property var manager
    property var runtime: null
    property var market: null
    property string view: "installed"
    title: qsTr("Plugins")
    dialogId: "plugins"
    resizable: true
    initialFocus: list
    defaultButton: closeButton
    signal copyRequested(string text)

    readonly property var ui: runtime ? runtime.ui : null
    readonly property var rows: manager ? manager.plugins : []
    property string filter: ""
    readonly property var shown: {
        const words = dialog.filter.trim().toLocaleLowerCase()
        return words.length === 0 ? rows : rows.filter(r => [r.name, r.authors, r.description]
                                                          .some(t => String(t).toLocaleLowerCase().includes(words)))
    }
    property string chosenId: ""
    readonly property var chosen: rows.find(r => r.id === chosenId) ?? null
    readonly property bool marketAvailable: market !== null && market.available
    readonly property var offers: market ? market.offers : ({})
    property var notNow: []
    readonly property var listings: market ? market.listings : []
    property string search: ""
    property string category: ""
    readonly property var found: {
        const words = dialog.search.trim().toLocaleLowerCase()
        return listings.filter(l => (dialog.category.length === 0 || l.categories.includes(dialog.category))
                                    && (words.length === 0 || [l.name, l.summary, l.authorName]
                                        .some(t => String(t).toLocaleLowerCase().includes(words))))
    }
    property string chosenListingId: ""
    readonly property var chosenListing: listings.find(l => l.id === chosenListingId) ?? null
    function showView(next) {
        view = next
        if (next === "market") {
            if (market.status === "" || market.status === "failed")
                market.refresh()
            if (!chosenListing && found.length > 0)
                chosenListingId = found[0].id
            marketList.forceActiveFocus()
        } else {
            list.forceActiveFocus()
        }
    }
    property bool automaticUpdates: settings !== null && settings.value("plugins.autoUpdate") === true
    Connections {
        target: dialog.settings
        function onChanged(id) {
            if (id === "plugins.autoUpdate")
                dialog.automaticUpdates = dialog.settings.value(id) === true
        }
    }
    property int statesVersion: 0
    Connections {
        target: dialog.ui
        function onPluginStatesChanged() { dialog.statesVersion++ }
    }
    function stateOf(row) {
        void dialog.statesVersion
        return row && dialog.ui ? dialog.ui.pluginState(row.id) : { status: "running", reason: "" }
    }
    function stateText(row) {
        if (row.revoked)
            return qsTr("Revoked")
        if (row.held.length > 0)
            return qsTr("%1 waits for you").arg(row.held)
        const status = dialog.stateOf(row).status
        if (status === "off")
            return qsTr("Turned off")
        if (status === "failed")
            return qsTr("Didn’t start")
        if (status === "stopped")
            return qsTr("Stopped")
        return row.unrestricted ? qsTr("Full trust") : ""
    }
    function signedText(row) {
        if (row.source === "marketplace")
            return qsTr("From the marketplace")
        return row.keyId.length > 0 ? qsTr("Unverified, signed by key %1").arg(row.keyId) : qsTr("Unverified, unsigned")
    }
    function choose(id) {
        chosenId = id
        list.currentIndex = shown.findIndex(r => r.id === id)
    }

    function show() { showPlugin(chosenId.length > 0 ? chosenId : (rows.length > 0 ? rows[0].id : "")) }
    function showPlugin(id) {
        filter = ""
        view = "installed"
        choose(rows.some(r => r.id === id) ? id : (rows.length > 0 ? rows[0].id : ""))
        open()
        list.forceActiveFocus()
    }
    function tell(title, text) {
        messages.ask({
            title: title,
            text: text,
            severity: "error",
            choices: [{ id: "ok", text: qsTr("OK"), role: "accept", isDefault: true }]
        }, () => {})
    }
    function trustText(review) {
        if (review.needsRetrust && review.keyId.length > 0)
            return qsTr("The signing key changed. The installed copy was signed by key %1, and this one by key %2. Install it only if you trust that the author changed keys.")
                .arg(review.pinnedKeyId, review.keyId)
        if (review.needsRetrust)
            return qsTr("This package isn’t signed, but the installed copy was signed by key %1. Install it only if you trust where it came from.")
                .arg(review.pinnedKeyId)
        if (review.keyId.length > 0)
            return qsTr("Unverified source, signed by key %1. Updates must be signed by the same key.").arg(review.keyId)
        return qsTr("Unverified: the package isn’t signed, so HikariSub can’t tell who made it.")
    }
    function reviewFile(url) { reviewPackage(url, "") }
    function reviewPackage(url, updating) {
        confirm(updating ? manager.reviewUpdate(url, updating) : manager.review(url))
    }
    function confirm(review) {
        if (!review.ok) {
            tell(qsTr("Plugin not installed"), review.message)
            return
        }
        const titles = {
            install: qsTr("Install plugin"),
            update: qsTr("Update plugin"),
            reinstall: qsTr("Reinstall plugin"),
            downgrade: qsTr("Downgrade plugin")
        }
        const questions = {
            install: qsTr("Install “%1” %2?").arg(review.name, review.version),
            update: qsTr("Update “%1” from %2 to %3?").arg(review.name, review.installedVersion, review.version),
            reinstall: qsTr("Reinstall “%1” %2?").arg(review.name, review.version),
            downgrade: qsTr("Replace “%1” %2 with the older %3?").arg(review.name, review.installedVersion, review.version)
        }
        const tagged = review.changes.map(c => "• " + c.text + " (" + (c.added ? qsTr("New") : qsTr("allowed now")) + ")")
        const asks = review.permissions.length === 0
                ? qsTr("It asks for no permissions beyond the active subtitle file.")
                : review.asksForMore ? qsTr("It asks for more than %1 does").arg(review.installedVersion) + "\n" + tagged.join("\n")
                : qsTr("It asks for these permissions") + "\n• " + review.permissions.join("\n• ")
        const changelog = (review.changelog || "").length > 0
                ? "\n\n" + qsTr("Changes in %1").arg(review.version) + "\n" + review.changelog : ""
        const held = dialog.rows.find(r => r.id === review.id && r.held === review.version) !== undefined
        const keep = held ? [{ id: "keep", text: qsTr("Keep %1").arg(review.installedVersion), role: "action" }] : []
        const fullAccess = review.marketplace && review.unrestricted
        const trust = review.marketplace
                ? (fullAccess ? qsTr("Install it only if you trust %1. It gets full access to your computer.").arg(review.signedBy) + "\n\n" : "")
                  + qsTr("Signed by %1 and checked against the marketplace.").arg(review.signedBy)
                : trustText(review)
        const warn = review.needsRetrust || fullAccess
        messages.ask({
            title: titles[review.change],
            text: questions[review.change],
            detail: trust + "\n\n" + asks + changelog + "\n\n" + dialog.basics,
            severity: warn || review.unrestricted ? "warning" : "question",
            choices: [
                { id: "install", text: review.needsRetrust ? qsTr("Trust and install")
                                     : fullAccess ? qsTr("Install with full access") : titles[review.change],
                  role: warn ? "destructive" : "accept", isDefault: !warn }
            ].concat(keep).concat([{ id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: warn }])
        }, answer => {
            if (answer !== "install") {
                dialog.manager.cancelReview()
                if (answer === "keep")
                    dialog.market.keepCurrent(review.id)
                return
            }
            const problem = dialog.manager.install(review.needsRetrust)
            if (problem.length > 0)
                dialog.tell(qsTr("Plugin not installed"), problem)
            else if (dialog.view === "installed")
                dialog.choose(review.id)
        })
    }
    readonly property string basics: qsTr("Every plugin can read and edit the document you’re working on, as one undo step.")
    Connections {
        target: dialog.market
        function onReviewed(review) { dialog.confirm(review) }
    }
    function askUninstall(row) {
        const keep = { id: "keep", text: qsTr("Uninstall, keep its data"), role: "accept" }
        const remove = { id: "remove", text: manager.hasData(row.id) ? qsTr("Uninstall and remove its data") : qsTr("Uninstall"),
                         role: "destructive" }
        messages.ask({
            title: qsTr("Uninstall plugin"),
            text: qsTr("Uninstall “%1”?").arg(row.name),
            detail: manager.hasData(row.id) ? qsTr("It has saved data in the user folder. Keep it to use it again if you reinstall.") : "",
            severity: "warning",
            choices: (manager.hasData(row.id) ? [remove, keep] : [remove])
                     .concat([{ id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }])
        }, answer => {
            if (answer === "cancel")
                return
            const problem = dialog.manager.uninstall(row.id, answer === "remove")
            if (problem.length > 0)
                dialog.tell(qsTr("Plugin not uninstalled"), problem)
            if (!dialog.chosen)
                dialog.choose(dialog.shown.length > 0 ? dialog.shown[0].id : "")
        })
    }



    MessageHost {
        id: messages
        objectName: "pluginManagerMessages"
        settings: dialog.settings
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 860
        Layout.preferredHeight: 560
        Layout.minimumWidth: 600
        spacing: 8
        RowLayout {
            spacing: 4
            Layout.fillWidth: true
            SegmentedButtons {
                objectName: "pluginManagerView"
                visible: dialog.marketAvailable
                choices: [{ text: dialog.market && dialog.market.updates > 0 ? qsTr("Installed %1").arg(dialog.market.updates)
                                                                             : qsTr("Installed"),
                            name: "pluginViewInstalled", iconRole: "plugins" },
                          { text: qsTr("Marketplace"), name: "pluginViewMarketplace", iconRole: "marketplace" }]
                currentIndex: dialog.view === "market" ? 1 : 0
                onActivated: index => dialog.showView(index === 1 ? "market" : "installed")
            }
            Item { Layout.fillWidth: true }
            IconToolButton {
                objectName: "pluginInstallFromFile"
                iconRole: "plugin-install"
                text: qsTr("Install from file…")
                enabled: dialog.manager && dialog.manager.available
                onClicked: {
                    installPicker.updating = ""
                    installPicker.open()
                }
            }
            IconToolButton {
                objectName: "pluginCheckUpdates"
                visible: dialog.marketAvailable
                iconRole: "check-updates"
                text: qsTr("Check for updates")
                enabled: dialog.market && dialog.market.status !== "loading"
                onClicked: dialog.market.refresh()
            }
            IconToolButton {
                id: gearButton
                objectName: "pluginUpdateOptions"
                visible: dialog.marketAvailable
                iconRole: "plugin-update"
                text: qsTr("Plugin updates")
                onClicked: updatesPopover.openAt(gearButton, dialog.contentItem)
            }
        }
        Label {
            objectName: "pluginManagerProblem"
            text: !dialog.manager || !dialog.manager.available ? qsTr("Plugins need a user folder, and this copy has none.")
                                                               : dialog.manager.problem
            visible: text.length > 0
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            visible: dialog.view === "installed"
            spacing: 12
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                spacing: 6
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                TextField {
                    objectName: "pluginManagerFilter"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Filter")
                    text: dialog.filter
                    selectByMouse: true
                    Accessible.name: qsTr("Filter plugins")
                    onTextEdited: dialog.filter = text
                    Keys.onDownPressed: list.forceActiveFocus()
                }
                Frame {
                    padding: 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    background: Rectangle { color: Theme.field; border.color: Theme.line }
                    Label {
                        objectName: "pluginManagerEmpty"
                        visible: dialog.shown.length === 0
                        anchors.centerIn: parent
                        width: parent.width - 32
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.muted
                        text: dialog.rows.length === 0 ? qsTr("No plugins installed. Install one from a .hikariplugin file.")
                                                       : qsTr("No plugin matches the filter.")
                    }
                    ListView {
                        id: list
                        objectName: "pluginManagerList"
                        anchors.fill: parent
                        clip: true
                        model: dialog.shown
                        activeFocusOnTab: true
                        highlightMoveDuration: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Installed plugins")
                        onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex < dialog.shown.length)
                                                   dialog.chosenId = dialog.shown[currentIndex].id
                        Keys.onDeletePressed: if (dialog.chosen) dialog.askUninstall(dialog.chosen)
                        Keys.onSpacePressed: if (dialog.chosen) dialog.manager.setEnabled(dialog.chosen.id, !dialog.chosen.enabled)
                        delegate: ItemDelegate {
                            id: row
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 48
                            highlighted: modelData.id === dialog.chosenId
                            focusPolicy: Qt.NoFocus
                            onClicked: dialog.choose(modelData.id)
                            Accessible.role: Accessible.ListItem
                            Accessible.name: modelData.name + " " + modelData.version
                            Accessible.description: [dialog.signedText(modelData), rowState.text].filter(t => t.length > 0).join(", ")
                            Accessible.selected: highlighted
                            background: Rectangle {
                                color: row.highlighted ? Theme.select : "transparent"
                                Rectangle { visible: row.highlighted; width: 3; height: parent.height; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                PluginIcon { row: row.modelData; size: 24 }
                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label {
                                        text: row.modelData.name + " " + row.modelData.version
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: row.modelData.enabled ? Theme.text : Theme.muted
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        objectName: "pluginTrust_" + row.modelData.id
                                        text: dialog.signedText(row.modelData)
                                        elide: Text.ElideRight
                                        font: dialog.fontAt(12)
                                        color: Theme.muted
                                        Layout.fillWidth: true
                                    }
                                }
                                Label {
                                    id: rowState
                                    objectName: "pluginState_" + row.modelData.id
                                    visible: text.length > 0
                                    text: dialog.stateText(row.modelData)
                                    font: dialog.fontAt(12)
                                    color: row.modelData.revoked || ["failed", "stopped"].includes(dialog.stateOf(row.modelData).status) ? Theme.danger
                                         : row.modelData.held.length > 0 ? Theme.accent
                                         : row.modelData.unrestricted && row.modelData.enabled ? Theme.warning : Theme.muted
                                }
                            }
                        }
                    }
                }
            }
            ScrollView {
                id: detailsScroll
                objectName: "pluginDetails"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                visible: dialog.chosen !== null
                Loader {
                    width: detailsScroll.availableWidth
                    active: dialog.chosen !== null
                    sourceComponent: PluginDetails { row: dialog.chosen }
                }
            }
            Item {
                visible: dialog.chosen === null
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
        RowLayout {
            visible: dialog.view === "market"
            spacing: 12
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                spacing: 6
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                RowLayout {
                    spacing: 6
                    Layout.fillWidth: true
                    TextField {
                        objectName: "marketSearch"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search plugins")
                        text: dialog.search
                        selectByMouse: true
                        Accessible.name: qsTr("Search plugins")
                        onTextEdited: dialog.search = text
                        Keys.onDownPressed: marketList.forceActiveFocus()
                    }
                    ComboBox {
                        objectName: "marketCategory"
                        model: [qsTr("All categories")].concat(dialog.market ? dialog.market.categories : [])
                        currentIndex: Math.max(0, model.indexOf(dialog.category))
                        Accessible.name: qsTr("Category")
                        onActivated: index => dialog.category = index === 0 ? "" : model[index]
                    }
                }
                Frame {
                    padding: 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    background: Rectangle { color: Theme.field; border.color: Theme.line }
                    ColumnLayout {
                        objectName: "marketStatus"
                        visible: dialog.found.length === 0
                        anchors.centerIn: parent
                        width: parent.width - 32
                        spacing: 8
                        Label {
                            objectName: "marketStatusText"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: dialog.market && dialog.market.status === "failed" ? Theme.danger : Theme.muted
                            text: !dialog.market ? ""
                                : dialog.market.status === "failed" ? dialog.market.problem
                                : dialog.market.status === "loading" ? qsTr("Loading the marketplace…")
                                : dialog.listings.length === 0 ? qsTr("The marketplace lists no plugins yet.")
                                : qsTr("No plugin matches the search.")
                        }
                        IconTextButton {
                            objectName: "marketRetry"
                            visible: dialog.market && dialog.market.status === "failed"
                            Layout.alignment: Qt.AlignHCenter
                            iconRole: "restart"
                            text: qsTr("Try again")
                            onClicked: dialog.market.refresh()
                        }
                    }
                    ListView {
                        id: marketList
                        objectName: "marketList"
                        anchors.fill: parent
                        clip: true
                        model: dialog.found
                        currentIndex: dialog.found.findIndex(l => l.id === dialog.chosenListingId)
                        activeFocusOnTab: true
                        highlightMoveDuration: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Marketplace plugins")
                        Keys.onUpPressed: if (currentIndex > 0) dialog.chosenListingId = dialog.found[currentIndex - 1].id
                        Keys.onDownPressed: if (currentIndex < dialog.found.length - 1) dialog.chosenListingId = dialog.found[currentIndex + 1].id
                        delegate: ItemDelegate {
                            id: listed
                            required property var modelData
                            width: ListView.view.width
                            height: 52
                            highlighted: modelData.id === dialog.chosenListingId
                            focusPolicy: Qt.NoFocus
                            onClicked: dialog.chosenListingId = modelData.id
                            Accessible.role: Accessible.ListItem
                            Accessible.name: modelData.name
                            Accessible.description: modelData.summary
                            Accessible.selected: highlighted
                            background: Rectangle {
                                color: listed.highlighted ? Theme.select : "transparent"
                                Rectangle { visible: listed.highlighted; width: 3; height: parent.height; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                PluginIcon { row: listed.modelData; size: 32 }
                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    RowLayout {
                                        spacing: 6
                                        Layout.fillWidth: true
                                        Label {
                                            text: listed.modelData.name
                                            textFormat: Text.PlainText
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Badge {
                                            visible: listed.modelData.unrestricted
                                            iconRole: "plugin-full-access"
                                            text: qsTr("Unrestricted")
                                        }
                                    }
                                    Label {
                                        text: listed.modelData.summary
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font: dialog.fontAt(12)
                                        color: Theme.muted
                                        Layout.fillWidth: true
                                    }
                                }
                                Label {
                                    objectName: "marketRowState_" + listed.modelData.id
                                    visible: text.length > 0
                                    text: listed.modelData.action === "installed" ? qsTr("Installed")
                                        : listed.modelData.action === "update" ? qsTr("Update")
                                        : ""
                                    font: dialog.fontAt(12)
                                    color: Theme.muted
                                }
                            }
                        }
                    }
                }
            }
            ScrollView {
                id: listingScroll
                objectName: "marketDetails"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                visible: dialog.chosenListing !== null
                Loader {
                    width: listingScroll.availableWidth
                    active: dialog.chosenListing !== null
                    sourceComponent: ListingDetails { listing: dialog.chosenListing }
                }
            }
            Item {
                visible: dialog.chosenListing === null
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    component Badge: RowLayout {
        id: badge
        property string iconRole
        property string text
        spacing: 3
        Icon { iconRole: badge.iconRole; size: 12 }
        Label {
            text: badge.text
            font: dialog.fontAt(11)
            color: Theme.warning
        }
    }

    component Fact: Label {
        color: Theme.muted
    }

    component ListingDetails: ColumnLayout {
        id: market
        required property var listing
        spacing: 8
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            PluginIcon { row: market.listing; size: 48 }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                RowLayout {
                    spacing: 6
                    Label {
                        objectName: "marketDetailsName"
                        text: market.listing.name
                        textFormat: Text.PlainText
                        font: dialog.fontAt(15, Font.DemiBold)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Badge {
                        visible: market.listing.unrestricted
                        iconRole: "plugin-full-access"
                        text: qsTr("Unrestricted")
                    }
                }
                Label {
                    text: market.listing.authorName + " · " + market.listing.version
                    textFormat: Text.PlainText
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            RowLayout {
                objectName: "marketInstalled"
                visible: market.listing.action === "installed"
                spacing: 4
                Icon { iconRole: "plugin-installed" }
                Label {
                    text: qsTr("Installed")
                    color: Theme.muted
                }
            }
            PrimaryButton {
                objectName: "marketInstall"
                visible: market.listing.action === "install" || market.listing.action === "update"
                enabled: dialog.market.downloading.length === 0
                text: dialog.market.downloading === market.listing.id ? qsTr("Downloading…")
                    : market.listing.action === "update" ? qsTr("Update to %1").arg(market.listing.version)
                    : qsTr("Install")
                onClicked: dialog.market.download(market.listing.id)
            }
        }
        Label {
            objectName: "marketNeedsNewer"
            visible: market.listing.action === "incompatible"
            text: qsTr("It needs HikariSub %1 or later. Update HikariSub to install it.").arg(market.listing.needs)
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: market.listing.description.length > 0 ? market.listing.description : market.listing.summary
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Flow {
            visible: market.listing.screenshots.length > 0
            spacing: 8
            Layout.fillWidth: true
            Repeater {
                model: market.listing.screenshots
                Image {
                    required property var modelData
                    source: modelData.url
                    width: 200
                    height: 112
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: modelData.caption
                }
            }
        }
        Heading { text: qsTr("Permissions") }
        Repeater {
            model: market.listing.permissions.length > 0 ? market.listing.permissions
                                                         : [qsTr("Nothing beyond the basics: no network, no folders, no other documents.")]
            Label {
                required property string modelData
                text: "• " + modelData
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Label {
            text: dialog.basics
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Heading {
            visible: market.listing.changelog.length > 0
            text: qsTr("What’s new in %1").arg(market.listing.version)
        }
        Label {
            visible: market.listing.changelog.length > 0
            text: market.listing.changelog
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        GridLayout {
            columns: 2
            columnSpacing: 16
            rowSpacing: 4
            Layout.topMargin: 6
            Fact { visible: market.listing.needs.length > 0; text: qsTr("Needs") }
            Label {
                visible: market.listing.needs.length > 0
                text: qsTr("HikariSub %1 or later").arg(market.listing.needs)
            }
            Fact { text: qsTr("Licence") }
            Label { text: market.listing.license; textFormat: Text.PlainText }
            Fact { text: qsTr("Updated") }
            Label { text: market.listing.published }
            Fact { text: qsTr("Source") }
            LinkButton {
                objectName: "marketSource"
                iconRole: "open-link"
                text: market.listing.repo.replace(/^https:\/\//, "")
                onClicked: dialog.market.openSource(market.listing.id)
            }
        }
    }

    component PluginIcon: Item {
        id: pluginIcon
        required property var row
        property int size: 24
        implicitWidth: size
        implicitHeight: size
        Image {
            id: picture
            anchors.fill: parent
            visible: status === Image.Ready
            source: pluginIcon.row && pluginIcon.row.icon ? pluginIcon.row.icon : ""
            sourceSize: Qt.size(pluginIcon.size, pluginIcon.size)
            fillMode: Image.PreserveAspectFit
        }
        Icon {
            anchors.centerIn: parent
            visible: picture.status !== Image.Ready
            iconRole: "plugins"
            size: Math.min(pluginIcon.size, 24)
        }
    }

    component Heading: Label {
        font.bold: true
        Layout.topMargin: 6
        Layout.fillWidth: true
    }

    component PluginDetails: ColumnLayout {
        id: details
        required property var row
        readonly property var runState: dialog.stateOf(row)
        readonly property var offer: dialog.offers[row.id] ?? ({ update: "", move: false })
        spacing: 6
        InlineBar {
            objectName: "pluginMoveBar"
            visible: details.offer.move && !dialog.notNow.includes(details.row.id)
            iconRole: "marketplace"
            closable: false
            text: qsTr("“%1” is in the marketplace, signed with the same key. Move it there to get updates.").arg(details.row.name)
            Layout.fillWidth: true
            Button {
                objectName: "pluginMoveNotNow"
                text: qsTr("Not now")
                onClicked: dialog.notNow = dialog.notNow.concat([details.row.id])
            }
            Button {
                objectName: "pluginMove"
                text: qsTr("Move to the marketplace")
                font.bold: true
                enabled: dialog.market.downloading.length === 0
                onClicked: dialog.market.download(details.row.id)
            }
        }
        InlineBar {
            objectName: "pluginRevokedBar"
            visible: details.row.revoked
            severity: "error"
            closable: false
            text: qsTr("The marketplace revoked version %1, so HikariSub turned it off: %2").arg(details.row.version, details.row.revokedReason)
            Layout.fillWidth: true
            Button {
                objectName: "pluginInstallLastGood"
                visible: details.row.lastGood.length > 0
                text: qsTr("Install %1").arg(details.row.lastGood)
                font.bold: true
                enabled: dialog.market !== null && dialog.market.downloading.length === 0
                onClicked: dialog.market.installLastGood(details.row.id)
            }
            Button {
                objectName: "pluginRevokedUninstall"
                text: qsTr("Uninstall…")
                onClicked: dialog.askUninstall(details.row)
            }
        }
        InlineBar {
            objectName: "pluginHeldBar"
            visible: details.row.held.length > 0 && !details.row.revoked
            iconRole: "plugin-update"
            closable: false
            text: qsTr("%1 asks for more than %2 does, so it waits for you.").arg(details.row.held, details.row.version)
            Layout.fillWidth: true
            Button {
                objectName: "pluginHeldReview"
                text: dialog.market && dialog.market.downloading === details.row.id ? qsTr("Downloading…") : qsTr("Review…")
                font.bold: true
                enabled: dialog.market !== null && dialog.market.downloading.length === 0
                onClicked: dialog.market.download(details.row.id)
            }
        }
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            opacity: details.row.revoked ? 0.6 : 1
            PluginIcon { row: details.row; size: 32 }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Label {
                        objectName: "pluginDetailsName"
                        text: details.row.name + " " + details.row.version
                        textFormat: Text.PlainText
                        font: dialog.fontAt(15, Font.DemiBold)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                    }
                    LinkButton {
                        objectName: "pluginRollBack"
                        iconRole: "undo"
                        visible: details.row.previous.length > 0 && !details.row.revoked
                        text: qsTr("Roll back to %1").arg(details.row.previous)
                        onClicked: {
                            const problem = dialog.manager.rollBack(details.row.id)
                            if (problem.length > 0)
                                dialog.tell(qsTr("Plugin not rolled back"), problem)
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                Label {
                    visible: text.length > 0
                    text: details.row.authors
                    textFormat: Text.PlainText
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Switch {
                objectName: "pluginEnabled_" + details.row.id
                text: qsTr("On")
                enabled: !details.row.revoked
                checked: details.row.enabled
                onToggled: {
                    const problem = dialog.manager.setEnabled(details.row.id, checked)
                    if (problem.length > 0)
                        dialog.tell(qsTr("Plugin not changed"), problem)
                    checked = Qt.binding(() => details.row.enabled)
                }
            }
        }
        Label {
            visible: text.length > 0
            text: details.row.description
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            spacing: 4
            Icon { iconRole: "plugin-unverified"; size: 14 }
            Label {
                objectName: "pluginDetailsTrust"
                text: dialog.signedText(details.row) + (details.row.unrestricted ? " · " + qsTr("Full trust") : "")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        CheckBox {
            objectName: "pluginAutoUpdate_" + details.row.id
            visible: details.row.source === "marketplace" && dialog.marketAvailable
            text: qsTr("Install updates automatically")
            enabled: dialog.automaticUpdates
            checked: details.row.autoUpdate
            onToggled: {
                const problem = dialog.manager.setAutoUpdate(details.row.id, checked)
                if (problem.length > 0)
                    dialog.tell(qsTr("Plugin not changed"), problem)
                checked = Qt.binding(() => details.row.autoUpdate)
            }
        }
        Rectangle {
            objectName: "pluginDetailsError"
            visible: ["failed", "stopped"].includes(details.runState.status)
            Layout.fillWidth: true
            implicitHeight: errorColumn.implicitHeight + 16
            color: Theme.field
            border.color: Theme.danger
            ColumnLayout {
                id: errorColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                spacing: 6
                Label {
                    text: details.runState.status === "failed" ? qsTr("It didn’t start.") : qsTr("It stopped.")
                    color: Theme.danger
                    font.bold: true
                }
                Label {
                    text: details.runState.reason
                    textFormat: Text.PlainText
                    font.family: "monospace"
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
                RowLayout {
                    spacing: 8
                    IconTextButton {
                        objectName: "pluginStartAgain"
                        iconRole: "restart"
                        text: qsTr("Start again")
                        enabled: dialog.runtime !== null
                        onClicked: dialog.runtime.restart(details.row.id)
                    }
                    IconTextButton {
                        objectName: "pluginCopyError"
                        iconRole: "edit-copy"
                        text: qsTr("Copy")
                        onClicked: dialog.copyRequested(details.runState.reason)
                    }
                }
            }
        }
        Heading { text: qsTr("Permissions") }
        Repeater {
            model: details.row.permissions.length > 0 ? details.row.permissions
                                                      : [qsTr("It asks for no permissions beyond the active subtitle file.")]
            Label {
                required property string modelData
                text: "• " + modelData
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Heading {
            visible: details.row.folders.length > 0
            text: qsTr("Remembered folders")
        }
        Repeater {
            model: details.row.folders
            RowLayout {
                id: folderRow
                required property var modelData
                spacing: 8
                Layout.fillWidth: true
                Label {
                    text: qsTr("%1: %2").arg(folderRow.modelData.title, folderRow.modelData.path)
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                IconTextButton {
                    objectName: "pluginForgetFolder_" + folderRow.modelData.id
                    iconRole: "remove"
                    text: qsTr("Forget")
                    tip: qsTr("%1 asks again the next time it needs this folder").arg(details.row.name)
                    onClicked: dialog.manager.forgetFolder(details.row.id, folderRow.modelData.id)
                }
            }
        }
        Heading {
            visible: details.row.adds.length > 0
            text: qsTr("What it adds")
        }
        Repeater {
            model: details.row.adds
            Label {
                required property string modelData
                text: "• " + modelData
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Heading {
            visible: details.row.settings.length > 0
            text: qsTr("Settings")
        }
        Repeater {
            model: details.row.settings
            SettingField {
                pluginId: details.row.id
                Layout.fillWidth: true
            }
        }
        RowLayout {
            spacing: 8
            Layout.topMargin: 12
            Layout.fillWidth: true
            IconTextButton {
                objectName: "pluginUpdate"
                visible: details.offer.update.length > 0
                iconRole: "plugin-update"
                text: dialog.market && dialog.market.downloading === details.row.id ? qsTr("Downloading…")
                                                                                      : qsTr("Update to %1").arg(details.offer.update)
                enabled: dialog.market && dialog.market.downloading.length === 0
                onClicked: dialog.market.download(details.row.id)
            }
            IconTextButton {
                objectName: "pluginUpdateFromFile"
                iconRole: "plugin-update"
                text: qsTr("Update from file…")
                onClicked: {
                    installPicker.updating = details.row.id
                    installPicker.open()
                }
            }
            IconTextButton {
                objectName: "pluginOpenDataFolder"
                iconRole: "show-in-folder"
                text: qsTr("Open data folder")
                onClicked: dialog.manager.openDataFolder(details.row.id)
            }
            Item { Layout.fillWidth: true }
            DangerButton {
                objectName: "pluginUninstall_" + details.row.id
                text: qsTr("Uninstall…")
                onClicked: dialog.askUninstall(details.row)
            }
        }
    }

    component SettingField: RowLayout {
        id: field
        required property var modelData
        required property string pluginId
        spacing: 8
        function keep(value) {
            const problem = dialog.manager.setSetting(field.pluginId, field.modelData.id, JSON.stringify(value))
            if (problem.length > 0)
                dialog.tell(qsTr("Setting not kept"), problem)
        }
        Label {
            visible: field.modelData.type !== "boolean"
            text: field.modelData.title
            textFormat: Text.PlainText
            Layout.preferredWidth: 160
            elide: Text.ElideRight
        }
        Loader {
            Layout.fillWidth: field.modelData.type === "string" || field.modelData.type === "choice"
            sourceComponent: field.modelData.type === "boolean" ? checkSetting
                           : field.modelData.type === "choice" ? choiceSetting
                           : field.modelData.type === "string" ? textSetting : numberSetting
        }
        Component {
            id: checkSetting
            CheckBox {
                objectName: "pluginSetting_" + field.modelData.id
                text: field.modelData.title
                checked: field.modelData.value === true
                onToggled: field.keep(checked)
            }
        }
        Component {
            id: choiceSetting
            ComboBox {
                objectName: "pluginSetting_" + field.modelData.id
                model: field.modelData.options
                currentIndex: field.modelData.options.indexOf(field.modelData.value)
                Accessible.name: field.modelData.title
                onActivated: index => field.keep(field.modelData.options[index])
            }
        }
        Component {
            id: textSetting
            TextField {
                objectName: "pluginSetting_" + field.modelData.id
                text: field.modelData.value
                selectByMouse: true
                Accessible.name: field.modelData.title
                onEditingFinished: if (text !== field.modelData.value) field.keep(text)
            }
        }
        Component {
            id: numberSetting
            TextField {
                objectName: "pluginSetting_" + field.modelData.id
                readonly property bool whole: field.modelData.type === "integer"
                text: String(field.modelData.value)
                implicitWidth: 100
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                Accessible.name: field.modelData.title
                validator: whole ? intValidator : doubleValidator
                IntValidator { id: intValidator }
                DoubleValidator { id: doubleValidator; notation: DoubleValidator.StandardNotation }
                onEditingFinished: {
                    const value = whole ? parseInt(text, 10) : parseFloat(text.replace(",", "."))
                    if (isNaN(value) || value === field.modelData.value)
                        text = String(field.modelData.value)
                    else
                        field.keep(value)
                }
            }
        }
    }

    Popover {
        id: updatesPopover
        objectName: "pluginUpdatesPopover"
        title: qsTr("Plugin updates")
        showTitle: true
        CheckBox {
            id: checkUpdates
            objectName: "pluginCheckUpdatesSetting"
            text: qsTr("Check for plugin updates")
            checked: dialog.settings ? dialog.settings.value("plugins.checkUpdates") === true : false
            onToggled: {
                dialog.settings.setValue("plugins.checkUpdates", checked)
                checked = Qt.binding(() => dialog.settings.value("plugins.checkUpdates") === true)
            }
        }
        Label {
            text: qsTr("At start and once a day. HikariSub downloads the whole list and never sends which plugins you have.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.maximumWidth: 300
            Layout.leftMargin: checkUpdates.indicator ? checkUpdates.indicator.width + checkUpdates.spacing : 0
        }
        CheckBox {
            id: autoUpdate
            objectName: "pluginAutoUpdateSetting"
            text: qsTr("Install updates automatically")
            enabled: checkUpdates.checked
            checked: dialog.settings ? dialog.settings.value("plugins.autoUpdate") === true : false
            onToggled: {
                dialog.settings.setValue("plugins.autoUpdate", checked)
                checked = Qt.binding(() => dialog.settings.value("plugins.autoUpdate") === true)
            }
        }
        Label {
            text: qsTr("Only for marketplace plugins, once they finish what they’re doing. An update that asks for more waits for you.")
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.maximumWidth: 300
            Layout.leftMargin: autoUpdate.indicator ? autoUpdate.indicator.width + autoUpdate.spacing : 0
        }
    }
    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "pluginManagerClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
    // Qt reads the pattern from the filter's label.
    Dialogs.FileDialog {
        id: installPicker
        objectName: "pluginInstallPicker"
        property string updating: ""
        title: qsTr("Choose a plugin package")
        nameFilters: [qsTr("HikariSub plugins (*.hikariplugin)")]
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: dialog.reviewPackage(selectedFile, updating)
    }
}
