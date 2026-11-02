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
    title: qsTr("Plugins")
    dialogId: "plugins"
    modal: true
    initialFocus: list
    defaultButton: closeButton
    readonly property var rows: manager ? manager.plugins : []

    function show() {
        list.currentIndex = rows.length > 0 ? 0 : -1
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
    function reviewFile(url) {
        const review = manager.review(url)
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
                list.currentIndex = dialog.rows.findIndex(r => r.id === review.id)
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
            list.currentIndex = Math.min(list.currentIndex, dialog.rows.length - 1)
        })
    }

    MessageHost {
        id: messages
        objectName: "pluginManagerMessages"
        settings: dialog.settings
    }

    Label {
        objectName: "pluginManagerProblem"
        text: !dialog.manager || !dialog.manager.available ? qsTr("Plugins need a user folder, and this copy has none.")
                                                           : dialog.manager.problem
        visible: text.length > 0
        color: Theme.danger
        wrapMode: Text.WordWrap
        Layout.maximumWidth: 420
        Accessible.role: Accessible.AlertMessage
    }
    Frame {
        padding: 1
        Layout.preferredWidth: 420
        Layout.preferredHeight: 260
        background: Rectangle { color: Theme.field; border.color: Theme.line }
        Label {
            objectName: "pluginManagerEmpty"
            visible: dialog.rows.length === 0
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.muted
            text: qsTr("No plugins installed. Install one from a .hikariplugin file.")
        }
        ListView {
            id: list
            objectName: "pluginManagerList"
            anchors.fill: parent
            clip: true
            model: dialog.rows
            activeFocusOnTab: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Installed plugins")
            Keys.onDeletePressed: if (currentIndex >= 0) dialog.askUninstall(dialog.rows[currentIndex])
            Keys.onSpacePressed: if (currentIndex >= 0) {
                const row = dialog.rows[currentIndex]
                dialog.manager.setEnabled(row.id, !row.enabled)
            }
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 52
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                onClicked: list.currentIndex = index
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + " " + modelData.version
                Accessible.description: trust.text
                Accessible.selected: highlighted
                background: Rectangle {
                    color: row.highlighted ? Theme.select : "transparent"
                    Rectangle { visible: row.highlighted; width: 3; height: parent.height; color: Theme.accent }
                }
                contentItem: RowLayout {
                    spacing: 8
                    CheckBox {
                        objectName: "pluginEnabled_" + row.modelData.id
                        checked: row.modelData.enabled
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Enabled")
                        onToggled: dialog.manager.setEnabled(row.modelData.id, checked)
                    }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: row.modelData.name + " " + row.modelData.version
                            elide: Text.ElideRight
                            color: row.modelData.enabled ? Theme.text : Theme.muted
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Icon {
                                iconRole: "plugin-unverified"
                                size: 14
                            }
                            Label {
                                id: trust
                                objectName: "pluginTrust_" + row.modelData.id
                                text: (row.modelData.keyId.length > 0 ? qsTr("Unverified, signed by key %1").arg(row.modelData.keyId)
                                                                      : qsTr("Unverified, unsigned"))
                                      + (row.modelData.unrestricted ? " · " + qsTr("Full trust") : "")
                                elide: Text.ElideRight
                                font: dialog.fontAt(12)
                                color: Theme.muted
                                Layout.fillWidth: true
                            }
                        }
                    }
                    IconToolButton {
                        objectName: "pluginUninstall_" + row.modelData.id
                        iconRole: "delete"
                        text: qsTr("Uninstall")
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.askUninstall(row.modelData)
                    }
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
            onClicked: installPicker.open()
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
        title: qsTr("Choose a plugin package")
        nameFilters: [qsTr("HikariSub plugins (*.hikariplugin)")]
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: dialog.reviewFile(selectedFile)
    }
}
