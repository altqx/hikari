// O3: "Import legacy settings" (docs/qt/proposals/settings-import.md), a
// rewrite window without a legacy counterpart: the legacy installations
// found (or a folder the user chooses), the review plan of the chosen one
// (each record's source, value, destination, current value, disposition and
// why), the changes to import, Import (in effect at the next start) and
// Roll back to the settings before the last import. Colours are the
// palette's (the theme layer), never settings of their own.
// PU7 (#309): a window on DG1's frame (resizable, modal to the main window)
// and three steps (PU7-import-wizard): choose the installation (choosing one
// reads it), review (Conflicts, the rows that can be chosen, or All, every
// record with its status; the current row's details under the table), and
// the import's result with Roll back…, which PK1's message box asks first.
// While an import is in effect the first step offers Roll back… too.
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

    // 1: the installation, 2: the review, 3: the result.
    property int step: 1
    // Conflicts (the rows that can be chosen) or every record.
    property bool conflictsOnly: true
    property string search: ""
    // How many rows the last import wrote (the plan is proposed again after it).
    property int imported: 0
    readonly property var conflicts: importer.rows.filter(r => r.selectable)
    // The rows the filter and the search show. importer.rows changes only
    // with the plan; choosing rows changes importer.chosenIds, so the list
    // keeps its delegates and where it is scrolled.
    readonly property var shownRows: {
        const needle = search.trim().toLowerCase()
        return (conflictsOnly ? conflicts : importer.rows).filter(r => needle.length === 0
            || [r.key, r.destination, r.value, r.current, r.source].some(t => (t || "").toLowerCase().includes(needle)))
    }
    readonly property var currentRow: rowList.currentIndex >= 0 && rowList.currentIndex < shownRows.length
                                      ? shownRows[rowList.currentIndex] : null
    readonly property int chosenCount: importer.chosenIds.length

    // Settings edited since a waiting import was staged, kept at the next
    // start over the import's values (settings-import.md step 5).
    property var keptEdits: []

    function openDialog() {
        step = 1
        conflictsOnly = true
        search = ""
        keptEdits = importer.keptEdits()
        importer.discover()
        open()
        if (importer.roots.length > 0)
            chooseRoot(0)
    }
    function chooseRoot(index) {
        rootList.currentIndex = index
        if (index >= 0 && importer.available)
            importer.choose(importer.roots[index].path)
    }
    // The status a record shows under All.
    function statusOf(row) {
        if (row.selectable)
            return qsTr("Conflict")
        switch (row.disposition) {
        case "unchanged":
        case "missing":
            return qsTr("Same")
        case "unresolved":
            return qsTr("Unresolved")
        case "read":
            return qsTr("File read")
        default: // superseded, excluded, retired
            return qsTr("Not here")
        }
    }
    // Sets the tick of every conflict shown.
    function chooseShown(on) {
        for (const row of shownRows)
            if (row.selectable)
                importer.setChosen(row.id, on)
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
            if (answer === "rollback")
                dialog.importer.rollback()
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
        onAccepted: {
            if (dialog.importer.addRoot(selectedFolder.toString()))
                dialog.chooseRoot(dialog.importer.roots.length - 1)
        }
    }

    // The steps, the current one filled in the accent, those done ticked.
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
                    Accessible.name: qsTr("Step %1 of 3: %2").arg(stepItem.number).arg(stepItem.modelData)
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

        // 1. The installation.
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
                visible: dialog.importer.ambiguousEncoding || dialog.importer.interpretation > 0
                spacing: 8
                Layout.topMargin: 8
                Label { text: qsTr("Files that are not UTF-8 are read as") }
                ComboBox {
                    objectName: "settingsImportInterpretation"
                    Layout.preferredWidth: 200
                    model: [qsTr("Choose..."), qsTr("Latin-1 (ISO 8859-1)"), qsTr("Windows-1252")]
                    currentIndex: dialog.importer.interpretation
                    onActivated: index => dialog.importer.interpretation = index
                }
            }
            Item { Layout.fillHeight: true }
            // An import waiting for the next start, or one in effect: Roll back… here too.
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

        // 2. The review.
        ColumnLayout {
            spacing: 6
            Label {
                text: qsTr("Review what to import")
                font: dialog.fontAt(14, Font.DemiBold)
            }
            Label {
                text: dialog.conflictsOnly
                      ? qsTr("A conflict is a setting the legacy installation sets differently from yours. The proposed choice is ticked; untick what you want to keep.")
                      : qsTr("Every record read, with its status. Only conflicts can be imported.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Layout.topMargin: 6
                SegmentedButtons {
                    objectName: "settingsImportFilter"
                    choices: [{ text: qsTr("Conflicts %1").arg(dialog.conflicts.length), name: "settingsImportConflicts" },
                              { text: qsTr("All %1").arg(dialog.importer.rows.length), name: "settingsImportAll" }]
                    currentIndex: dialog.conflictsOnly ? 0 : 1
                    onActivated: index => {
                        dialog.conflictsOnly = index === 0
                        rowList.currentIndex = dialog.shownRows.length > 0 ? 0 : -1
                    }
                }
                TextField {
                    objectName: "settingsImportSearch"
                    placeholderText: qsTr("Search settings")
                    Accessible.name: qsTr("Search settings")
                    text: dialog.search
                    onTextEdited: dialog.search = text
                    Layout.preferredWidth: 240
                }
                Item { Layout.fillWidth: true }
                Label {
                    objectName: "settingsImportSummary"
                    color: Theme.muted
                    text: qsTr("%1 of %2 chosen").arg(dialog.chosenCount).arg(dialog.conflicts.length)
                }
            }
            Frame {
                padding: 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: Rectangle { color: Theme.field; border.color: Theme.line }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        color: Theme.raised
                        implicitHeight: 28
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            Item { implicitWidth: 16 }
                            Label { text: qsTr("Setting"); color: Theme.muted; Layout.fillWidth: true }
                            Label { text: qsTr("Legacy"); color: Theme.muted; Layout.preferredWidth: 160 }
                            Label { text: qsTr("Now"); color: Theme.muted; Layout.preferredWidth: 160 }
                            Label { text: qsTr("Status"); color: Theme.muted; Layout.preferredWidth: 84 }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    }
                    ListView {
                        id: rowList
                        objectName: "settingsImportRows"
                        clip: true
                        model: dialog.shownRows
                        keyNavigationEnabled: true
                        activeFocusOnTab: true
                        highlightMoveDuration: 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Settings to review")
                        ScrollBar.vertical: ScrollBar {}
                        // Space ticks the current row.
                        Keys.onSpacePressed: {
                            const row = dialog.currentRow
                            if (row && row.selectable)
                                dialog.importer.setChosen(row.id, dialog.importer.chosenIds.indexOf(row.id) < 0)
                        }
                        delegate: ItemDelegate {
                            id: rowItem
                            required property var modelData
                            required property int index
                            objectName: "settingsImportRow_" + modelData.id
                            width: ListView.view.width
                            height: 28
                            highlighted: ListView.isCurrentItem
                            focusPolicy: Qt.NoFocus
                            onClicked: rowList.currentIndex = index
                            Accessible.role: Accessible.ListItem
                            Accessible.name: (modelData.destination || modelData.key) + ", " + dialog.statusOf(modelData)
                            background: Rectangle {
                                color: rowItem.highlighted ? Theme.select : "transparent"
                                Rectangle { visible: rowItem.highlighted; width: 3; height: parent.height; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                spacing: 10
                                CheckBox {
                                    objectName: "settingsImportChoose_" + rowItem.modelData.id
                                    padding: 0
                                    enabled: rowItem.modelData.selectable
                                    opacity: enabled ? 1 : 0.4
                                    focusPolicy: Qt.NoFocus
                                    function isChosen() { return dialog.importer.chosenIds.indexOf(rowItem.modelData.id) >= 0 }
                                    checked: isChosen()
                                    onToggled: {
                                        rowList.currentIndex = rowItem.index
                                        dialog.importer.setChosen(rowItem.modelData.id, checked)
                                        // The click broke the binding: "Proposed choice" and
                                        // "All conflicts" still reach this row.
                                        checked = Qt.binding(isChosen)
                                    }
                                    Accessible.name: rowItem.modelData.destination || rowItem.modelData.key
                                }
                                Label {
                                    text: rowItem.modelData.destination || rowItem.modelData.key
                                    color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: rowItem.modelData.value
                                    color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 160
                                }
                                Label {
                                    text: rowItem.modelData.current
                                    color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 160
                                }
                                Item {
                                    implicitHeight: 20
                                    Layout.preferredWidth: 84
                                    Rectangle {
                                        objectName: "settingsImportStatus_" + rowItem.modelData.id
                                        readonly property string text: dialog.statusOf(rowItem.modelData)
                                        width: statusLabel.implicitWidth + 12
                                        height: 20
                                        radius: 2
                                        color: Theme.panel
                                        border.color: rowItem.modelData.selectable ? Theme.accent
                                                    : rowItem.modelData.disposition === "unresolved" ? Theme.warning : Theme.line
                                        Label {
                                            id: statusLabel
                                            anchors.centerIn: parent
                                            text: parent.text
                                            font: dialog.fontAt(11, rowItem.modelData.selectable ? Font.DemiBold : Font.Normal)
                                            color: rowItem.modelData.selectable ? Theme.text
                                                 : rowItem.modelData.disposition === "unresolved" ? Theme.warning : Theme.muted
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: rowList.count === 0
                    text: dialog.conflictsOnly && dialog.search.length === 0
                          ? qsTr("No conflicts: the legacy settings are the same as yours or need no choice.")
                          : qsTr("Nothing matches.")
                    color: Theme.muted
                }
            }
            // The current row's details: its file and key, where it goes, why.
            // Three lines high whatever the row, so choosing one never moves
            // the table above it.
            Rectangle {
                color: Theme.raised
                border.color: Theme.line
                implicitHeight: Math.ceil(detailsMetrics.lineSpacing * 3) + 16
                Layout.fillWidth: true
                FontMetrics { id: detailsMetrics; font: details.font }
                Label {
                    id: details
                    objectName: "settingsImportDetails"
                    x: 10
                    y: 8
                    width: parent.width - 20
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    readonly property var row: dialog.currentRow
                    text: !row ? qsTr("Choose a row to see where it comes from.")
                        : [row.source + " › " + row.key + (row.raw.length > 0 ? " = " + row.raw : "")
                           + (row.destination.length > 0 ? "  →  " + row.destination : ""),
                           row.reason,
                           row.paths.length > 0 ? qsTr("Paths not found, kept as they are: %1").arg(row.paths.join(", ")) : ""]
                          .filter(t => t.length > 0).join("\n")
                }
            }
            RowLayout {
                spacing: 16
                LinkButton {
                    objectName: "settingsImportProposed"
                    iconRole: "restore-default"
                    text: qsTr("Proposed choice")
                    onClicked: dialog.importer.chooseAll("proposed", true)
                }
                LinkButton {
                    objectName: "settingsImportAllConflicts"
                    iconRole: "commit"
                    text: qsTr("All conflicts")
                    onClicked: dialog.chooseShown(true)
                }
                LinkButton {
                    objectName: "settingsImportNone"
                    iconRole: "clear"
                    text: qsTr("None")
                    onClicked: dialog.chooseShown(false)
                }
            }
        }

        // 3. The result.
        ColumnLayout {
            spacing: 6
            RowLayout {
                spacing: 16
                Layout.fillWidth: true
                Icon {
                    iconRole: dialog.importer.pending ? "commit" : "notice-warning"
                    size: 32
                    color: dialog.importer.pending ? Theme.accent : Theme.warning
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    spacing: 6
                    Layout.fillWidth: true
                    Label {
                        objectName: "settingsImportResult"
                        text: dialog.importer.pending ? qsTr("%n setting(s) imported.", "", dialog.imported)
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
                rowList.currentIndex = dialog.shownRows.length > 0 ? 0 : -1
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
