pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.ScriptProperties"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "scriptInfoOnSaveDialog"
    title: qsTr("Script properties")
    dialogId: "scriptInfoOnSave"
    guideId: "scriptProperties"
    modal: true
    defaultButton: okButton
    property string documentTitle: ""
    property var rows: []
    property var kept: ({})
    property string choice: ""
    property var done: null

    function labelFor(key) {
        return {
            "Title": qsTr("Title"),
            "Original Script": qsTr("Author"),
            "Original Translation": qsTr("Translator"),
            "Original Editing": qsTr("Proofreading"),
            "Original Timing": qsTr("Timer"),
            "Script Updated By": qsTr("Editing")
        }[key] ?? key
    }
    function ask(title, changes, from, answered) {
        documentTitle = title
        rows = changes
        const all = {}
        for (const row of changes)
            all[row.key] = true
        kept = all
        choice = ""
        done = answered
        openFrom(from)
    }
    function finish(how) {
        choice = how
        accept()
    }
    function keptKeys() {
        return rows.filter(row => kept[row.key] === true).map(row => row.key)
    }
    onAccepted: {
        const answered = done
        done = null
        if (answered)
            answered(choice, choice === "ok" ? keptKeys() : [])
    }
    onRejected: {
        const answered = done
        done = null
        if (answered)
            answered("cancel", [])
    }

    ColumnLayout {
        spacing: 12
        Label {
            objectName: "scriptInfoOnSaveText"
            text: qsTr("Saving “%1” changes these script properties.").arg(dialog.documentTitle)
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.preferredWidth: 460
        }
        GridLayout {
            columns: 3
            columnSpacing: 12
            rowSpacing: 6
            Repeater {
                model: dialog.rows
                delegate: CheckBox {
                    id: box
                    required property var modelData
                    required property int index
                    objectName: "scriptInfoOnSaveField" + index
                    Layout.row: index
                    Layout.column: 0
                    text: dialog.labelFor(modelData.key)
                    checked: dialog.kept[modelData.key] === true
                    onToggled: dialog.kept = Object.assign({}, dialog.kept, {[modelData.key]: checked})
                }
            }
            Repeater {
                model: dialog.rows
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index
                    Layout.column: 1
                    Layout.maximumWidth: 200
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.strikeout: modelData.from !== ""
                    text: modelData.from !== "" ? modelData.from : "—"
                }
            }
            Repeater {
                model: dialog.rows
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index
                    Layout.column: 2
                    Layout.maximumWidth: 200
                    elide: Text.ElideRight
                    text: modelData.to !== "" ? modelData.to : "—"
                }
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: okButton
            objectName: "scriptInfoOnSaveOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.finish("ok")
        },
        Button {
            objectName: "scriptInfoOnSaveSkip"
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.finish("skip")
        },
        Button {
            objectName: "scriptInfoOnSaveCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
