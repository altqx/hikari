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
        tagNames.text = app.columnTagNames()
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
    readonly property var plainRows: rows.filter(r => !r.tags)
    readonly property var tagRows: rows.filter(r => r.tags && !r.named)
    readonly property var namedTagsRow: rows.find(r => r.named) ?? null
    function boxes() {
        const out = []
        for (const repeater of [columnRepeater, tagRepeater])
            for (let i = 0; i < repeater.count; ++i)
                out.push(repeater.itemAt(i))
        if (namedTagsRow)
            out.push(namedTags)
        return out
    }
    function chosen() {
        let bits = 0
        for (const box of boxes())
            if (box.checked)
                bits |= box.bit
        return bits
    }
    function checkAll(on) {
        for (const box of boxes())
            box.checked = on
    }

    Label {
        objectName: "columnsText"
        text: columns.paste ? (columns.lineCount === 1 ? qsTr("Paste the clipboard into these columns of the selected line:")
                                                       : qsTr("Paste the clipboard into these columns of the %1 selected lines:").arg(columns.lineCount))
                            : (columns.lineCount === 1 ? qsTr("Columns to copy from the selected line")
                                                       : qsTr("Columns to copy from the %1 selected lines").arg(columns.lineCount))
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
                model: columns.plainRows
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    readonly property int bit: modelData.bit
                    objectName: "column" + index
                    text: modelData.label
                    checked: modelData.checked
                    Layout.fillWidth: true
                }
            }
        }
    }
    MenuHeading {
        objectName: "columnsTagsHeading"
        visible: columns.tagRows.length > 0
        text: qsTr("Tags")
        leftPadding: 0
    }
    Frame {
        visible: columns.tagRows.length > 0
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 0
                Layout.fillWidth: true
                Repeater {
                    id: tagRepeater
                    model: columns.tagRows
                    delegate: CheckBox {
                        required property var modelData
                        required property int index
                        readonly property int bit: modelData.bit
                        objectName: "columnTags" + index
                        text: modelData.label
                        checked: modelData.checked
                        Layout.fillWidth: true
                    }
                }
            }
            RowLayout {
                visible: columns.namedTagsRow !== null
                spacing: 8
                Layout.fillWidth: true
                CheckBox {
                    id: namedTags
                    readonly property int bit: columns.namedTagsRow ? columns.namedTagsRow.bit : 0
                    objectName: "columnNamedTags"
                    text: columns.namedTagsRow ? columns.namedTagsRow.label : ""
                    checked: columns.namedTagsRow ? columns.namedTagsRow.checked : false
                }
                TextField {
                    id: tagNames
                    objectName: "columnTagNames"
                    font.family: "monospace"
                    enabled: namedTags.checked
                    placeholderText: "\\blur, \\bord, \\3c"
                    Accessible.name: namedTags.text
                    Layout.fillWidth: true
                }
            }
            Label {
                text: qsTr("Paste puts each tag into the start block, replacing the same tag.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.maximumWidth: 352
            }
        }
    }
    RowLayout {
        spacing: 14
        IconTextButton {
            iconRole: "select-all"
            objectName: "columnsAll"
            flat: true
            text: qsTr("Select all")
            onClicked: columns.checkAll(true)
        }
        IconTextButton {
            iconRole: "clear"
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
                const names = tagNames.text
                if (forPaste)
                    columns.app.pasteColumns(bits, names)
                else
                    columns.app.copyColumns(bits, names)
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
