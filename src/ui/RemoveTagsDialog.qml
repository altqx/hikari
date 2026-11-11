pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Tags"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "removeTagsDialog"
    required property var app
    title: qsTr("Remove tags")
    dialogId: "removeTags"
    modal: true
    defaultButton: removeButton
    initialFocus: linesBox
    property var document: 0
    property var families: []
    property var cleanup: ({})

    readonly property var familyRows: [
        { name: qsTr("Colour"), tags: "\\c \\1c \\2c \\3c \\4c" },
        { name: qsTr("Alpha"), tags: "\\alpha \\1a \\2a \\3a \\4a" },
        { name: qsTr("Border"), tags: "\\bord \\xbord \\ybord" },
        { name: qsTr("Shadow"), tags: "\\shad \\xshad \\yshad" },
        { name: qsTr("Blur"), tags: "\\blur \\be" },
        { name: qsTr("Font"), tags: "\\fn \\fs \\fe" },
        { name: qsTr("Scale"), tags: "\\fscx \\fscy" },
        { name: qsTr("Spacing"), tags: "\\fsp" },
        { name: qsTr("Rotation"), tags: "\\frz \\frx \\fry" },
        { name: qsTr("Shear"), tags: "\\fax \\fay" },
        { name: qsTr("Bold, italic, underline, strikeout"), tags: "\\b \\i \\u \\s" },
        { name: qsTr("Position"), tags: "\\pos \\move" },
        { name: qsTr("Origin"), tags: "\\org" },
        { name: qsTr("Alignment"), tags: "\\an \\a" },
        { name: qsTr("Clip"), tags: "\\clip \\iclip" },
        { name: qsTr("Fade"), tags: "\\fad \\fade" },
        { name: qsTr("Transforms"), tags: "\\t" },
        { name: qsTr("Karaoke"), tags: "\\k \\K \\kf \\ko \\kt" },
        { name: qsTr("Reset"), tags: "\\r" },
        { name: qsTr("Wrap style"), tags: "\\q" },
        { name: qsTr("Drawing"), tags: "\\p \\pbo" },
        { name: qsTr("Other tags"), tags: qsTr("Any tag not listed") }
    ]
    readonly property var cleanupRows: [
        { key: "removeComments", name: qsTr("Remove comments"), help: qsTr("Blocks with no tag, such as {note}") },
        { key: "mergeBlocks", name: qsTr("Merge adjacent tag blocks"), help: qsTr("%1 becomes %2").arg("{\\b1}{\\i1}", "{\\b1\\i1}") },
        { key: "dropRedundant", name: qsTr("Drop overridden tags"),
          help: qsTr("A tag a later one in its block replaces, and repeats of %1, %2, %3 and %4").arg("\\pos", "\\org", "\\an", "\\fad") },
        { key: "cleanTags", name: qsTr("Clean Tags rules"),
          help: qsTr("What the bundled Clean Tags script does: merges blocks, moves line-wide tags to the start, drops empty blocks") },
        { key: "deleteEmptyLines", name: qsTr("Delete lines left empty") }
    ]

    readonly property var options: ({
        families: dialog.families,
        scope: ["all", "start", "inline"][blocksBox.currentIndex],
        lines: linesBox.currentIndex === 1 ? "file" : "selected",
        removeComments: dialog.cleanup.removeComments === true,
        mergeBlocks: dialog.cleanup.mergeBlocks === true,
        dropRedundant: dialog.cleanup.dropRedundant === true,
        cleanTags: dialog.cleanup.cleanTags === true,
        deleteEmptyLines: dialog.cleanup.deleteEmptyLines === true
    })
    readonly property var preview: visible ? app.tagRemovalPreview(options) : ({})
    readonly property int changes: (preview.changed ?? 0) + (preview.deleted ?? 0)

    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }

    function remembered() {
        try {
            return JSON.parse(dialog.settings ? dialog.settings.value("tags.removeOptions") || "{}" : "{}")
        } catch (e) {
            return {}
        }
    }
    function openDialog() {
        const o = remembered()
        families = Array.isArray(o.families) ? o.families.filter(f => Number.isInteger(f)) : []
        cleanup = typeof o.cleanup === "object" && o.cleanup !== null ? o.cleanup : {}
        linesBox.currentIndex = o.lines === "file" ? 1 : 0
        blocksBox.currentIndex = Math.max(0, ["all", "start", "inline"].indexOf(o.scope))
        document = app.editingDocument()
        open()
    }
    function setFamily(index, on) {
        const next = families.filter(f => f !== index)
        if (on)
            next.push(index)
        families = next
    }
    function setCleanup(key, on) {
        const next = Object.assign({}, cleanup)
        next[key] = on
        cleanup = next
    }
    function apply() {
        if (changes === 0 || app.editingDocument() !== document)
            return
        const result = app.removeTags(options)
        if (dialog.settings)
            dialog.settings.setValue("tags.removeOptions", JSON.stringify({
                families: families, cleanup: cleanup, lines: options.lines, scope: options.scope }))
        if (!result.ok)
            return
        close()
    }

    ColumnLayout {
        spacing: 12
        Layout.preferredWidth: 640
        RowLayout {
            spacing: 8
            Label { text: qsTr("Lines") }
            ComboBox {
                id: linesBox
                objectName: "removeTagsLines"
                model: [qsTr("Selected lines"), qsTr("Whole file, without comment lines")]
                Layout.preferredWidth: 250
                Accessible.name: qsTr("Lines")
            }
            Item { Layout.preferredWidth: 8 }
            Label { text: qsTr("Blocks") }
            ComboBox {
                id: blocksBox
                objectName: "removeTagsBlocks"
                model: [qsTr("All blocks"), qsTr("Start block"), qsTr("Inline blocks")]
                Layout.preferredWidth: 160
                Accessible.name: qsTr("Blocks")
            }
        }
        RowLayout {
            spacing: 8
            Label { text: qsTr("Tags"); font: dialog.fontAt(13, Font.DemiBold) }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "removeTagsAll"
                text: dialog.families.length === dialog.familyRows.length ? qsTr("Clear") : qsTr("Select all")
                flat: true
                onClicked: dialog.families = dialog.families.length === dialog.familyRows.length
                                             ? [] : dialog.familyRows.map((_, i) => i)
            }
        }
        GridLayout {
            columns: 3
            columnSpacing: 12
            rowSpacing: 0
            Repeater {
                model: dialog.familyRows
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "removeTagsFamily" + index
                    text: modelData.name
                    checked: dialog.families.indexOf(index) >= 0
                    onToggled: {
                        dialog.setFamily(index, checked)
                        checked = Qt.binding(() => dialog.families.indexOf(index) >= 0)
                    }
                    Layout.fillWidth: true
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: modelData.tags
                    Accessible.description: modelData.tags
                }
            }
        }
        Label { text: qsTr("Clean up"); font: dialog.fontAt(13, Font.DemiBold) }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 0
            Repeater {
                model: dialog.cleanupRows
                delegate: CheckBox {
                    required property var modelData
                    objectName: "removeTagsCleanup_" + modelData.key
                    text: modelData.name
                    checked: dialog.cleanup[modelData.key] === true
                    onToggled: {
                        dialog.setCleanup(modelData.key, checked)
                        checked = Qt.binding(() => dialog.cleanup[modelData.key] === true)
                    }
                    Layout.fillWidth: true
                    ToolTip.visible: hovered && !!modelData.help
                    ToolTip.delay: 500
                    ToolTip.text: modelData.help ?? ""
                    Accessible.description: modelData.help ?? ""
                }
            }
        }
        Rectangle {
            color: Theme.field
            border.color: Theme.line
            radius: 4
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            ListView {
                id: previewList
                objectName: "removeTagsPreview"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: dialog.preview.rows ?? []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Preview")
                delegate: Item {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: rowColumn.implicitHeight + 8
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("Line %1: %2").arg(modelData.row, modelData.deleted ? qsTr("deleted") : modelData.after)
                    Label {
                        id: number
                        x: 8; y: 4
                        width: 44
                        text: row.modelData.row
                        color: Theme.muted
                        font: dialog.fontAt(12)
                        horizontalAlignment: Text.AlignRight
                    }
                    ColumnLayout {
                        id: rowColumn
                        x: number.x + number.width + 10
                        y: 4
                        width: row.width - x - 8
                        spacing: 1
                        Label {
                            text: row.modelData.before
                            color: Theme.muted
                            font.family: "monospace"
                            font.pixelSize: 12
                            font.strikeout: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: row.modelData.deleted ? qsTr("Line deleted") : row.modelData.after
                            color: row.modelData.deleted ? Theme.danger : Theme.text
                            font.family: row.modelData.deleted ? Qt.application.font.family : "monospace"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: previewList.count === 0
                text: qsTr("No line changes")
                color: Theme.muted
            }
        }
    }

    leadingActions: [
        Label {
            objectName: "removeTagsSummary"
            text: {
                const changed = dialog.preview.changed ?? 0
                const deleted = dialog.preview.deleted ?? 0
                const parts = []
                if (changed > 0)
                    parts.push(qsTr("%n line(s) change", "", changed))
                if (deleted > 0)
                    parts.push(qsTr("%n deleted", "", deleted))
                if ((dialog.preview.more ?? 0) > 0)
                    parts.push(qsTr("first %n shown", "", previewList.count))
                return parts.join(" · ")
            }
            color: Theme.muted
            font: dialog.fontAt(12)
            Layout.alignment: Qt.AlignVCenter
        }
    ]
    buttons: [
        PrimaryButton {
            id: removeButton
            objectName: "removeTagsApply"
            text: qsTr("Remove")
            enabled: dialog.changes > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "removeTagsCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
