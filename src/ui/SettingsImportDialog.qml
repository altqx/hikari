pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.SettingsImport"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "settingsImportDialog"
    required property var importer
    title: qsTr("Import legacy settings")
    dialogId: "settingsImport"
    modal: true
    resizable: true
    defaultButton: step === 1 ? nextButton : step === 2 ? importButton : closeButton

    // MUST KILL: step is a magic int (1 installation, 2 review, 3 result)
    property int step: 1
    property int imported: 0
    readonly property int chosenCount: importer.chosenIds.length

    property var keptEdits: []
    property string rootProblem: ""
    property bool rolledBack: false

    function openDialog() {
        step = 1
        rootProblem = ""
        rolledBack = false
        review.reset()
        keptEdits = importer.keptEdits()
        importer.discover()
        open()
        if (importer.roots.length > 0)
            chooseRoot(0)
    }
    function addFolder(folder) {
        if (importer.addRoot(folder))
            chooseRoot(importer.roots.length - 1)
        else
            rootProblem = importer.status
    }
    function chooseRoot(index) {
        rootList.currentIndex = index
        if (index >= 0 && importer.available)
            rootProblem = importer.choose(importer.roots[index].path) ? "" : importer.status
    }
    function askRollBack() {
        const edits = importer.rollbackEdits()
        messages.ask({
            title: qsTr("Roll back import"),
            text: qsTr("Go back to the settings you had before the last import?"),
            detail: edits.length === 0
                    ? qsTr("They take effect the next time HikariSub starts. The legacy installation is not changed.")
                    : qsTr("These settings you changed since go back too: %1. The legacy installation is not changed.")
                          .arg(edits.join(", ")),
            severity: "warning",
            choices: [
                { id: "rollback", text: qsTr("Roll back"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, answer => {
            if (answer === "rollback" && dialog.importer.rollback())
                dialog.rolledBack = true
        })
    }

    MessageHost {
        id: messages
        objectName: "settingsImportMessages"
        settings: dialog.settings
    }
    Dialogs.FolderDialog {
        id: folderDialog
        objectName: "settingsImportFolderDialog"
        title: qsTr("Choose the folder of a legacy installation")
        onAccepted: dialog.addFolder(selectedFolder.toString())
    }

    RowLayout {
        objectName: "settingsImportSteps"
        spacing: 10
        Layout.fillWidth: true
        Layout.preferredWidth: 732
        Layout.bottomMargin: 6
        Repeater {
            model: [qsTr("Installation"), qsTr("Review"), qsTr("Import")]
            RowLayout {
                id: stepItem
                required property string modelData
                required property int index
                readonly property int number: index + 1
                spacing: 8
                Layout.fillWidth: number < 3
                Rectangle {
                    implicitWidth: 22
                    implicitHeight: 22
                    radius: 11
                    color: stepItem.number === dialog.step ? Theme.accent : Theme.panel
                    border.width: 1
                    border.color: stepItem.number <= dialog.step ? Theme.accent : Theme.control
                    Label {
                        anchors.centerIn: parent
                        visible: stepItem.number >= dialog.step
                        text: stepItem.number
                        font.weight: Font.DemiBold
                        color: stepItem.number === dialog.step ? Theme.onAccent : Theme.muted
                    }
                    Icon {
                        anchors.centerIn: parent
                        visible: stepItem.number < dialog.step
                        iconRole: "commit"
                        size: 14
                        color: Theme.accent
                    }
                }
                Label {
                    text: stepItem.modelData
                    font.weight: stepItem.number === dialog.step ? Font.DemiBold : Font.Normal
                    color: stepItem.number <= dialog.step ? Theme.text : Theme.muted
                    Accessible.name: qsTr("Step %1 of 3: %2").arg(stepItem.number, stepItem.modelData)
                }
                Rectangle {
                    visible: stepItem.number < 3
                    implicitHeight: 1
                    color: stepItem.number < dialog.step ? Theme.accent : Theme.line
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                }
            }
        }
    }

    StackLayout {
        currentIndex: dialog.step - 1
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 420

        ColumnLayout {
            spacing: 6
            Label {
                text: qsTr("Choose the legacy installation to import from")
                font: dialog.fontAt(14, Font.DemiBold)
            }
            Label {
                text: qsTr("HikariSub 0.0.x installations found on this computer. Nothing in them is changed.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Frame {
                padding: 1
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(4, Math.max(1, rootList.count)) * 52 + 2
                Layout.topMargin: 6
                background: Rectangle { color: Theme.field; border.color: Theme.line }
                ListView {
                    id: rootList
                    objectName: "settingsImportRoots"
                    anchors.fill: parent
                    clip: true
                    model: dialog.importer.roots
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    highlightMoveDuration: 0
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Legacy installations")
                    Keys.onUpPressed: dialog.chooseRoot(Math.max(0, currentIndex - 1))
                    Keys.onDownPressed: dialog.chooseRoot(Math.min(count - 1, currentIndex + 1))
                    delegate: ItemDelegate {
                        id: rootRow
                        required property var modelData
                        required property int index
                        objectName: "settingsImportRoot_" + index
                        width: ListView.view.width
                        height: 52
                        highlighted: ListView.isCurrentItem
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.chooseRoot(index)
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.path
                        Accessible.checked: highlighted
                        background: Rectangle {
                            color: rootRow.highlighted ? Theme.select : "transparent"
                            Rectangle { visible: rootRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                implicitWidth: 16
                                implicitHeight: 16
                                radius: 8
                                color: Theme.field
                                border.color: rootRow.highlighted ? Theme.accent : Theme.control
                                Rectangle {
                                    anchors.centerIn: parent
                                    visible: rootRow.highlighted
                                    width: 8; height: 8; radius: 4
                                    color: Theme.accent
                                }
                            }
                            Icon { iconRole: "folder-open" }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Label {
                                    text: rootRow.modelData.path
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: rootRow.modelData.files
                                    color: Theme.muted
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: rootList.count === 0
                    text: qsTr("No legacy installation was found. Choose its folder.")
                    color: Theme.muted
                }
            }
            IconTextButton {
                objectName: "settingsImportChooseFolder"
                iconRole: "folder-open"
                text: qsTr("Choose folder…")
                enabled: dialog.importer.available
                onClicked: folderDialog.open()
                Layout.topMargin: 2
            }
            RowLayout {
                visible: dialog.rootProblem.length > 0
                spacing: 6
                Layout.fillWidth: true
                Icon {
                    iconRole: "notice-error"
                    size: 14
                    color: Theme.danger
                    accentColor: Theme.danger
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                }
                Label {
                    objectName: "settingsImportRootProblem"
                    text: dialog.rootProblem
                    color: Theme.danger
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                }
            }
            RowLayout {
                visible: dialog.importer.ambiguousEncoding || dialog.importer.interpretation > 0
                spacing: 8
                Layout.topMargin: 8
                Label { text: qsTr("Files that are not UTF-8 are read as") }
                ComboBox {
                    objectName: "settingsImportInterpretation"
                    Layout.preferredWidth: 200
                    model: [qsTr("Choose…"), qsTr("Latin-1 (ISO 8859-1)"), qsTr("Windows-1252")]
                    currentIndex: dialog.importer.interpretation
                    onActivated: index => dialog.importer.interpretation = index
                }
            }
            Item { Layout.fillHeight: true }
            Rectangle {
                visible: dialog.importer.canRollBack || dialog.importer.pending
                color: Theme.raised
                border.color: Theme.line
                implicitHeight: barRow.implicitHeight + 12
                Layout.fillWidth: true
                RowLayout {
                    id: barRow
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8
                    Icon { iconRole: "about" }
                    Label {
                        objectName: "settingsImportState"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        text: dialog.importer.pending
                              ? (dialog.keptEdits.length === 0
                                 ? qsTr("An import waits for the next start.")
                                 : qsTr("An import waits for the next start. These settings changed since it was made keep your change: %1.")
                                       .arg(dialog.keptEdits.join(", ")))
                              : qsTr("The last import is in effect.")
                        Accessible.role: Accessible.AlertMessage
                    }
                    Button {
                        objectName: "settingsImportRollbackBar"
                        visible: dialog.importer.canRollBack
                        text: qsTr("Roll back…")
                        onClicked: dialog.askRollBack()
                    }
                }
            }
        }

        ColumnLayout {
            spacing: 6
            Label {
                text: qsTr("Review what to import")
                font: dialog.fontAt(14, Font.DemiBold)
            }
            Label {
                text: review.conflictsOnly
                      ? qsTr("A conflict is a setting the legacy installation sets differently from yours. The proposed choice is ticked; untick what you want to keep.")
                      : qsTr("Every record read, with its status. Only conflicts can be imported.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            SettingsImportReview {
                id: review
                importer: dialog.importer
                window: dialog
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            spacing: 6
            RowLayout {
                spacing: 16
                Layout.fillWidth: true
                Icon {
                    iconRole: dialog.rolledBack || dialog.importer.pending ? "commit" : "notice-warning"
                    size: 32
                    color: dialog.rolledBack || dialog.importer.pending ? Theme.accent : Theme.warning
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    spacing: 6
                    Layout.fillWidth: true
                    Label {
                        objectName: "settingsImportResult"
                        text: dialog.rolledBack ? qsTr("Rolled back to the settings before the last import.")
                            : dialog.importer.pending ? qsTr("%n setting(s) imported.", "", dialog.imported)
                            : qsTr("Nothing was imported.")
                        font: dialog.fontAt(14, Font.DemiBold)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "settingsImportStatus"
                        text: dialog.importer.status
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    leadingActions: [
        Button {
            objectName: "settingsImportBack"
            visible: dialog.step === 2
            text: qsTr("Back")
            onClicked: dialog.step = 1
        },
        Button {
            objectName: "settingsImportRollback"
            visible: dialog.step === 3 && dialog.importer.canRollBack
            text: qsTr("Roll back…")
            onClicked: dialog.askRollBack()
        }
    ]
    buttons: [
        PrimaryButton {
            id: nextButton
            objectName: "settingsImportNext"
            visible: dialog.step === 1
            enabled: dialog.importer.rows.length > 0
            text: qsTr("Next")
            onClicked: {
                dialog.step = 2
                review.showFirst()
            }
        },
        PrimaryButton {
            id: importButton
            objectName: "settingsImportImport"
            visible: dialog.step === 2
            enabled: dialog.chosenCount > 0
            text: qsTr("Import %n setting(s)", "", dialog.chosenCount)
            onClicked: {
                const count = dialog.chosenCount
                if (dialog.importer.importChosen()) {
                    dialog.imported = count
                    dialog.rolledBack = false
                    dialog.step = 3
                }
            }
        },
        PrimaryButton {
            id: closeButton
            objectName: "settingsImportClose"
            visible: dialog.step === 3
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.close()
        },
        Button {
            objectName: "settingsImportCancel"
            visible: dialog.step < 3
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
