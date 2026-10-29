pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Grid"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: columns
    objectName: "columnsWindow"
    required property var app
    dialogId: "columns"
    modal: true
    title: paste ? qsTr("Paste columns") : qsTr("Copy columns")
    defaultButton: ok
    property bool paste: false
    property var rows: []
    property int lineCount: 0
    function choose(forPaste) {
        paste = forPaste
        rows = app.columnChoices(forPaste)
        lineCount = app.selectedLineCount(forPaste)
        askedFor = editingTarget()
        open()
    }
    property var askedFor: null
    function editingTarget() {
        const tab = app.tabs[app.currentTab]
        return tab ? tab.id : null
    }
    Connections {
        target: columns.app
        function onTabsChanged() {
            if (columns.visible && columns.editingTarget() !== columns.askedFor)
                columns.close()
        }
    }
    function chosen() {
        let bits = 0
        for (let i = 0; i < columnRepeater.count; ++i)
            if (columnRepeater.itemAt(i).checked)
                bits |= rows[i].bit
        return bits
    }
    function checkAll(on) {
        for (let i = 0; i < columnRepeater.count; ++i)
            columnRepeater.itemAt(i).checked = on
    }

    Label {
        objectName: "columnsText"
        text: columns.paste ? (columns.lineCount === 1 ? qsTr("Paste the clipboard into these columns of the selected line:")
                                                       : qsTr("Paste the clipboard into these columns of the %1 selected lines:").arg(columns.lineCount))
                            : (columns.lineCount === 1 ? qsTr("Copy these columns of the selected line:")
                                                       : qsTr("Copy these columns of the %1 selected lines:").arg(columns.lineCount))
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.maximumWidth: 352
    }
    Frame {
        Layout.fillWidth: true
        GridLayout {
            anchors.fill: parent
            flow: GridLayout.TopToBottom
            rows: Math.ceil(columnRepeater.count / 2)
            columnSpacing: 16
            rowSpacing: 0
            Repeater {
                id: columnRepeater
                model: columns.rows
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "column" + index
                    text: modelData.label
                    checked: modelData.checked
                    Layout.fillWidth: true
                }
            }
        }
    }
    RowLayout {
        spacing: 14
        Button {
            objectName: "columnsAll"
            flat: true
            text: qsTr("Select all")
            onClicked: columns.checkAll(true)
        }
        Button {
            objectName: "columnsNone"
            flat: true
            text: qsTr("None")
            onClicked: columns.checkAll(false)
        }
    }

    buttons: [
        PrimaryButton {
            id: ok
            objectName: "columnsOk"
            text: columns.paste ? qsTr("Paste") : qsTr("Copy")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                const bits = columns.chosen()
                const forPaste = columns.paste
                columns.accept()
                if (forPaste)
                    columns.app.pasteColumns(bits)
                else
                    columns.app.copyColumns(bits)
            }
        },
        Button {
            objectName: "columnsCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: columns.close()
        }
    ]
}
