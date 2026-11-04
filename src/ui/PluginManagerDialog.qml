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
        return row.keyId.length > 0 ? qsTr("Unverified, signed by key %1").arg(row.keyId) : qsTr("Unverified, unsigned")
    }
    function choose(id) {
        chosenId = id
        list.currentIndex = shown.findIndex(r => r.id === id)
    }

    function show() { showPlugin(chosenId.length > 0 ? chosenId : (rows.length > 0 ? rows[0].id : "")) }
    function showPlugin(id) {
        filter = ""
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
        const review = updating ? manager.reviewUpdate(url, updating) : manager.review(url)
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
        const asks = review.permissions.length > 0
                ? qsTr("It asks for these permissions") + "\n• " + review.permissions.join("\n• ")
                : qsTr("It asks for no permissions beyond the active subtitle file.")
        messages.ask({
            title: titles[review.change],
            text: questions[review.change],
            detail: trustText(review) + "\n\n" + asks,
            severity: review.needsRetrust || review.unrestricted ? "warning" : "question",
            choices: [
                { id: "install", text: review.needsRetrust ? qsTr("Trust and install") : titles[review.change],
                  role: review.needsRetrust ? "destructive" : "accept", isDefault: !review.needsRetrust },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: review.needsRetrust }
            ]
        }, answer => {
            if (answer !== "install") {
                dialog.manager.cancelReview()
                return
            }
            const problem = dialog.manager.install(review.needsRetrust)
            if (problem.length > 0)
                dialog.tell(qsTr("Plugin not installed"), problem)
            else
                dialog.choose(review.id)
        })
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
        Layout.preferredWidth: 760
        Layout.preferredHeight: 520
        Layout.minimumWidth: 600
        spacing: 8
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
                                    color: ["failed", "stopped"].includes(dialog.stateOf(row.modelData).status) ? Theme.danger
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
        spacing: 6
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            PluginIcon { row: details.row; size: 32 }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    objectName: "pluginDetailsName"
                    text: details.row.name + " " + details.row.version
                    textFormat: Text.PlainText
                    font: dialog.fontAt(15, Font.DemiBold)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
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
                objectName: "pluginUpdateFromFile"
                iconRole: "plugin-install"
                text: qsTr("Update from file…")
                onClicked: {
                    installPicker.updating = details.row.id
                    installPicker.open()
                }
            }
            IconTextButton {
                objectName: "pluginOpenDataFolder"
                iconRole: "folder-open"
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

    leadingActions: [
        IconTextButton {
            objectName: "pluginInstallFromFile"
            iconRole: "plugin-install"
            text: qsTr("Install from file…")
            enabled: dialog.manager && dialog.manager.available
            onClicked: {
                installPicker.updating = ""
                installPicker.open()
            }
        }
    ]
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
