// O3: "Import legacy settings" (docs/qt/proposals/settings-import.md), a
// rewrite window without a legacy counterpart: the legacy installations
// found (or a folder the user chooses), the review plan of the chosen one
// (each record's source, value, destination, current value, disposition and
// why), the changes to import, Import (in effect at the next start) and
// Roll back to the settings before the last import. Colours are the
// palette's (the theme layer), never settings of their own.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "settingsImportDialog"
    required property var importer
    title: qsTr("Import legacy settings")
    header: IconDialogHeader { objectName: "settingsImportDialogTitle"; iconRole: "settings"; text: dialog.title }
    modal: true
    width: Math.min(parent ? parent.width - 40 : 900, 900)
    height: Math.min(parent ? parent.height - 40 : 640, 640)
    closePolicy: Popup.CloseOnEscape

    // The rows the filter shows. importer.rows changes only with the plan;
    // choosing rows changes importer.chosenIds, so the list keeps its
    // delegates and where it is scrolled.
    property string filter: ""
    readonly property var shownRows: {
        const out = []
        for (const row of importer.rows)
            if (filter === "" || row.disposition === filter)
                out.push(row)
        return out
    }

    // Settings edited since a waiting import was staged, kept at the next
    // start over the import's values (settings-import.md step 5).
    property var keptEdits: []

    function openDialog() {
        keptEdits = importer.keptEdits()
        importer.discover()
        open()
        if (importer.roots.length > 0)
            rootBox.currentIndex = 0
    }
    function readRoot() {
        if (rootBox.currentIndex >= 0)
            importer.choose(importer.roots[rootBox.currentIndex].path)
    }

    Dialogs.FolderDialog {
        id: folderDialog
        objectName: "settingsImportFolderDialog"
        title: qsTr("Choose the folder of a legacy installation")
        onAccepted: {
            if (dialog.importer.addRoot(selectedFolder.toString())) {
                rootBox.currentIndex = dialog.importer.roots.length - 1
                dialog.readRoot()
            }
        }
    }

    Dialog {
        id: rollbackQuestion
        objectName: "settingsImportRollbackQuestion"
        title: qsTr("Roll back")
        modal: true
        anchors.centerIn: parent
        property var edits: []
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 420
            wrapMode: Text.Wrap
            text: rollbackQuestion.edits.length === 0
                  ? qsTr("Go back to the settings before the last import?")
                  : qsTr("Go back to the settings before the last import? These changes made since will be replaced:\n%1")
                        .arg(rollbackQuestion.edits.join("\n"))
        }
        onAccepted: dialog.importer.rollback()
    }

    contentItem: ColumnLayout {
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Installation:") }
            ComboBox {
                id: rootBox
                objectName: "settingsImportRoots"
                Layout.fillWidth: true
                model: dialog.importer.roots
                textRole: "path"
            }
            IconToolButton {
                objectName: "settingsImportChooseFolder"
                iconRole: "folder-open"
                text: qsTr("Choose folder…")
                onClicked: folderDialog.open()
            }
            Button {
                objectName: "settingsImportRead"
                text: qsTr("Read")
                enabled: rootBox.currentIndex >= 0 && dialog.importer.available
                onClicked: dialog.readRoot()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: rootBox.currentIndex >= 0
            elide: Text.ElideRight
            text: rootBox.currentIndex >= 0 ? dialog.importer.roots[rootBox.currentIndex].files : ""
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.importer.ambiguousEncoding || dialog.importer.interpretation > 0
            Label { text: qsTr("Files that are not UTF-8 are read as:") }
            ComboBox {
                objectName: "settingsImportInterpretation"
                model: [qsTr("Choose..."), qsTr("Latin-1 (ISO 8859-1)"), qsTr("Windows-1252")]
                currentIndex: dialog.importer.interpretation
                onActivated: index => dialog.importer.interpretation = index
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Show:") }
            ComboBox {
                id: filterBox
                objectName: "settingsImportFilter"
                readonly property var values: ["", "change", "missing", "unchanged", "superseded", "unresolved", "excluded", "retired", "read"]
                model: [qsTr("All"), qsTr("Changes"), qsTr("Missing"), qsTr("Unchanged"), qsTr("Superseded"),
                        qsTr("Unresolved"), qsTr("Excluded"), qsTr("Retired"), qsTr("Files read")]
                onActivated: index => dialog.filter = values[index]
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "settingsImportProposed"
                text: qsTr("Proposed choice")
                enabled: dialog.importer.rows.length > 0
                onClicked: dialog.importer.chooseAll("proposed", true)
            }
            Button {
                objectName: "settingsImportAllChanges"
                text: qsTr("All changes")
                enabled: dialog.importer.rows.length > 0
                onClicked: dialog.importer.chooseAll("change", true)
            }
        }
        ListView {
            id: rowList
            objectName: "settingsImportRows"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dialog.shownRows
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: rowFrame
                required property var modelData
                required property int index
                objectName: "settingsImportRow_" + modelData.id
                width: rowList.width - 12
                padding: 4
                RowLayout {
                    anchors.fill: parent
                    CheckBox {
                        objectName: "settingsImportChoose_" + rowFrame.modelData.id
                        enabled: rowFrame.modelData.selectable
                        function isChosen() { return dialog.importer.chosenIds.indexOf(rowFrame.modelData.id) >= 0 }
                        checked: isChosen()
                        onToggled: {
                            dialog.importer.setChosen(rowFrame.modelData.id, checked)
                            // The click broke the binding: "Proposed choice" and
                            // "All changes" still reach this row.
                            checked = Qt.binding(isChosen)
                        }
                        Accessible.name: rowFrame.modelData.key
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.bold: true
                            text: rowFrame.modelData.key + (rowFrame.modelData.destination.length > 0
                                                            ? "  →  " + rowFrame.modelData.destination : "")
                                  + "   [" + rowFrame.modelData.disposition + "]"
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            visible: rowFrame.modelData.value.length > 0 || rowFrame.modelData.current.length > 0
                            text: qsTr("Legacy: %1    Now: %2").arg(rowFrame.modelData.value).arg(rowFrame.modelData.current)
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            opacity: 0.75
                            text: rowFrame.modelData.source + (rowFrame.modelData.raw.length > 0 ? "  " + rowFrame.modelData.raw : "")
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: rowFrame.modelData.reason.length > 0
                            text: rowFrame.modelData.reason
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: rowFrame.modelData.paths.length > 0
                            text: qsTr("Unresolved paths, kept as they are: %1").arg(rowFrame.modelData.paths.join(", "))
                        }
                    }
                }
            }
        }
        Label {
            objectName: "settingsImportSummary"
            Layout.fillWidth: true
            text: dialog.importer.summary
        }
        Label {
            objectName: "settingsImportStatus"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: text.length > 0
            text: dialog.importer.pending && dialog.importer.status.length === 0
                  ? (dialog.keptEdits.length === 0
                     ? qsTr("An import waits for the next start.")
                     : qsTr("An import waits for the next start. These settings changed since it was made keep your change:\n%1")
                           .arg(dialog.keptEdits.join("\n")))
                  : dialog.importer.status
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "settingsImportRollback"
            text: qsTr("Roll back")
            enabled: dialog.importer.canRollBack
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: {
                rollbackQuestion.edits = dialog.importer.rollbackEdits()
                rollbackQuestion.open()
            }
        }
        Button {
            objectName: "settingsImportImport"
            text: qsTr("Import")
            enabled: dialog.importer.rows.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: dialog.importer.importChosen()
        }
        Button {
            objectName: "settingsImportClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    }
}
