pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "automationResetDialog"
    required property var reset
    readonly property bool moved: reset.outcome === "moved"
    title: moved ? qsTr("Automation folder reset") : qsTr("Automation folder not reset")
    dialogId: "automationReset"
    modal: true
    defaultButton: okButton

    function iconOf(state) {
        return state === "installed" ? "run-applied" : state === "failed" ? "notice-error" : "run-busy"
    }

    Label {
        objectName: "automationResetWhy"
        text: dialog.moved
            ? qsTr("The karaoke templater and several scripts are now built into HikariSub. Old copies could replace or break them, so your automation folder was reset.")
            : qsTr("Your automation folder wasn’t reset: %1. Nothing was moved or deleted. HikariSub tries again at the next start.").arg(dialog.reset.reason)
        wrapMode: Text.WordWrap
        Layout.preferredWidth: 440
    }
    ColumnLayout {
        visible: dialog.moved
        spacing: 4
        Layout.preferredWidth: 440
        Label {
            text: qsTr("Moved")
            font.bold: true
        }
        Label {
            objectName: "automationResetMoved"
            text: dialog.reset.moved.join("\n")
            Layout.fillWidth: true
            elide: Text.ElideMiddle
        }
        Label {
            text: qsTr("Backup")
            font.bold: true
            Layout.topMargin: 6
        }
        TextEdit {
            objectName: "automationResetBackup"
            text: dialog.reset.backup
            readOnly: true
            selectByMouse: true
            color: Theme.text
            wrapMode: TextEdit.WrapAnywhere
            Layout.fillWidth: true
            Accessible.name: qsTr("Backup")
        }
        Label {
            visible: dialog.reset.builtIn.length > 0
            text: qsTr("Now built in")
            font.bold: true
            Layout.topMargin: 6
        }
        Repeater {
            model: dialog.reset.builtIn
            Label {
                required property var modelData
                objectName: "automationResetBuiltIn_" + modelData.nameSpace
                text: qsTr("%1: use %2").arg(modelData.nameSpace, modelData.command)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        Repeater {
            model: dialog.reset.reinstalling ? dialog.reset.scripts : []
            RowLayout {
                id: row
                required property var modelData
                objectName: "automationResetScript_" + modelData.nameSpace
                spacing: 6
                Layout.fillWidth: true
                Layout.topMargin: 2
                Icon {
                    iconRole: dialog.iconOf(row.modelData.state)
                    size: 14
                    color: row.modelData.state === "failed" ? Theme.danger : Theme.text
                    accentColor: row.modelData.state === "failed" ? Theme.danger : IconTheme.accent
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                }
                Label {
                    objectName: "automationResetScriptText_" + row.modelData.nameSpace
                    text: row.modelData.state === "failed" ? qsTr("%1: %2").arg(row.modelData.name, row.modelData.error)
                        : row.modelData.state === "installed" ? qsTr("%1 is installed.").arg(row.modelData.name)
                        : row.modelData.name
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                IconTextButton {
                    objectName: "automationResetRetry_" + row.modelData.nameSpace
                    visible: row.modelData.state === "failed"
                    iconRole: "refresh"
                    text: qsTr("Try again")
                    onClicked: dialog.reset.retry(row.modelData.nameSpace)
                }
            }
        }
        Label {
            text: qsTr("Scripts you copied in by hand aren’t reinstalled. They’re still in the backup folder.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: 6
        }
    }
    leadingActions: [
        IconTextButton {
            objectName: "automationResetReinstall"
            visible: dialog.moved && dialog.reset.reinstallCount > 0
            iconRole: "reinstall-scripts"
            text: qsTr("Reinstall %n script(s)", "", dialog.reset.reinstallCount)
            onClicked: dialog.reset.reinstall()
        },
        IconTextButton {
            objectName: "automationResetOpenBackup"
            visible: dialog.moved
            iconRole: "show-in-folder"
            text: qsTr("Open backup folder")
            onClicked: dialog.reset.openBackup()
        }
    ]
    buttons: [
        PrimaryButton {
            id: okButton
            objectName: "automationResetOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        }
    ]
}
