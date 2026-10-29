pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Documents"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: unsaved
    required property var app
    property var updates: null
    property SettingsStore settings: null
    property var pickerFolder: picker => ""
    property var rememberPickerFolder: (picker, file) => {}
    property var confirmOverDisk: (ids, then, cancelled) => then()
    property var rows: []
    property var choices: []
    property string problem: ""
    property string kind: ""
    readonly property DialogWindow window: windowLoader.item as DialogWindow
    readonly property bool opened: (window !== null && window.visible) || question.opened
    readonly property int saving: choices.filter(c => c.save).length
    visible: false

    function review(list) {
        // MUST KILL: choices must be set before rows; the row delegates read them
        choices = list.map(r => ({ id: r.id, save: true, path: "" }))
        rows = list
        problem = ""
        kind = app.closeKind()
        if (list.length === 1)
            askOne()
        else
            showWindow()
    }
    function close() {
        question.close()
        if (window && window.visible)
            window.accept()
    }
    function fail(text) {
        problem = text
        question.close()
        showWindow()
    }
    function cancel() {
        if (!app.quitApproved && updates)
            updates.installer.cancelRestart()
        app.cancelClose()
    }
    function showWindow() {
        windowLoader.active = true
        window.open()
    }
    function choiceAt(i) { return choices[i] || { save: false, path: "" } }
    function setSave(i, save) {
        const list = choices.slice()
        list[i] = Object.assign({}, list[i], { save: save })
        choices = list
    }
    function setAll(save) { choices = choices.map(c => Object.assign({}, c, { save: save })) }
    function proceed() {
        const i = choices.findIndex((c, k) => c.save && rows[k].untitled && c.path.length === 0)
        if (i >= 0)
            pick(i, () => proceed())
        else
            resolve()
    }
    function resolve() {
        confirmOverDisk(choices.filter(c => c.save).map(c => c.id), () => app.resolveClose(choices), () => {
            if (!window || !window.visible)
                cancel()
        })
    }
    function discardAll() {
        setAll(false)
        app.resolveClose(choices)
    }
    function pick(i, then) {
        const v = app.saveDialogValuesFor(rows[i].id)
        saveAs.row = i
        saveAs.then = then || null
        saveAs.title = qsTr("Save “%1” as").arg(rows[i].title)
        saveAs.nameFilters = [v.filter, qsTr("All files (*)")]
        if (v.folder.toString() !== "")
            saveAs.currentFolder = v.folder
        else if (pickerFolder("subtitles").length > 0)
            saveAs.currentFolder = pickerFolder("subtitles")
        // FileDialog keeps the last selectedFile between opens, so it is set even when empty.
        saveAs.selectedFile = v.file
        saveAs.open()
    }

    function words(n, name) {
        switch (kind) {
        case "quit":
            return { title: qsTr("Quit"),
                     many: qsTr("Save the changes to %n documents before quitting?", "", n),
                     one: qsTr("Save the changes to “%1” before quitting?").arg(name),
                     save: qsTr("Save and quit"), discard: qsTr("Quit without saving") }
        case "all":
            return { title: qsTr("Close all tabs"),
                     many: qsTr("Save the changes to %n documents before closing them?", "", n),
                     one: qsTr("Save the changes to “%1” before closing?").arg(name),
                     save: qsTr("Save and close"), discard: qsTr("Close without saving") }
        case "session":
            return { title: qsTr("Load session"),
                     many: qsTr("Save the changes to %n documents before loading the session?", "", n),
                     one: qsTr("Save the changes to “%1” before loading the session?").arg(name),
                     save: qsTr("Save and load"), discard: qsTr("Load without saving") }
        case "close":
        case "tab":
            return { title: qsTr("Close tab"), many: "",
                     one: qsTr("Save the changes to “%1” before closing?").arg(name),
                     save: qsTr("Save and close"), discard: qsTr("Close without saving") }
        case "new":
            return { title: qsTr("Remove subtitles from the editor"), many: "",
                     one: qsTr("Save the changes to “%1” before removing them?").arg(name),
                     save: qsTr("Save and remove"), discard: qsTr("Remove without saving") }
        default:
            return { title: kind === "files" ? qsTr("Open files") : qsTr("Open subtitles"), many: "",
                     one: qsTr("Save the changes to “%1” before replacing them?").arg(name),
                     save: qsTr("Save and open"), discard: qsTr("Open without saving") }
        }
    }

    function askOne() {
        const row = rows[0]
        const w = words(1, row.title)
        question.ask({
            title: w.title,
            text: w.one,
            detail: qsTr("Changes you don’t save are lost."),
            severity: "warning",
            choices: [
                { id: "save", text: row.untitled ? qsTr("Save as…") : qsTr("Save"), role: "accept", isDefault: true },
                { id: "discard", text: qsTr("Don’t save"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject" }
            ]
        }, id => {
            if (id === "save") {
                if (row.untitled)
                    unsaved.pick(0, () => unsaved.resolve())
                else
                    unsaved.resolve()
            } else if (id === "discard") {
                unsaved.discardAll()
            } else {
                unsaved.cancel()
            }
        })
    }

    LazyMessageBox {
        id: question
        name: "closeQuestion"
        settings: unsaved.settings
    }

    FileDialog {
        id: saveAs
        objectName: "closeSaveAs"
        property int row: -1
        property var then: null
        fileMode: FileDialog.SaveFile
        onAccepted: {
            unsaved.rememberPickerFolder("subtitles", selectedFile)
            const list = unsaved.choices.slice()
            list[row] = Object.assign({}, list[row], { path: unsaved.app.localPath(selectedFile) })
            unsaved.choices = list
            const next = then
            then = null
            if (next)
                next()
        }
        onRejected: {
            then = null
            if (unsaved.rows.length === 1 && !(unsaved.window && unsaved.window.visible))
                unsaved.askOne()
        }
    }

    Loader {
        id: windowLoader
        active: false
        sourceComponent: DialogWindow {
            id: reviewWindow
            objectName: "closeReview"
            dialogId: "unsavedChanges"
            settings: unsaved.settings
            modal: true
            resizable: true
            readonly property var w: unsaved.words(unsaved.rows.length, unsaved.rows.length > 0 ? unsaved.rows[0].title : "")
            title: w.title
            onRejected: unsaved.cancel()

            Label {
                objectName: "closeReviewText"
                text: reviewWindow.w.many.length > 0 ? reviewWindow.w.many : reviewWindow.w.one
                font: reviewWindow.fontAt(14, Font.DemiBold)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.preferredWidth: 560
                Layout.topMargin: 6
            }
            Label {
                text: qsTr("Changes you don’t save are lost.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.topMargin: -6
            }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Math.min(list.contentHeight, 2 * 48) + 2
                Layout.preferredHeight: Math.min(list.contentHeight, 5 * 48) + 2
                ListView {
                    id: list
                    objectName: "closeReviewRows"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: unsaved.rows
                    activeFocusOnTab: false
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "closeRow" + index
                        width: ListView.view.width
                        implicitHeight: 48
                        Rectangle {
                            visible: row.index > 0
                            width: parent.width
                            height: 1
                            color: Theme.line
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 14
                            spacing: 12
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Label {
                                    text: row.modelData.title
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    spacing: 8
                                    Label {
                                        objectName: "closeFolder" + row.index
                                        text: row.modelData.untitled
                                              ? (unsaved.choiceAt(row.index).path.length > 0
                                                 ? unsaved.choiceAt(row.index).path : qsTr("Not saved yet"))
                                              : row.modelData.folder
                                        font: {
                                            const f = unsaved.window.fontAt(12)
                                            f.italic = row.modelData.untitled && unsaved.choiceAt(row.index).path.length === 0
                                            return f
                                        }
                                        color: Theme.muted
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                    Button {
                                        objectName: "closeChooseFile" + row.index
                                        visible: row.modelData.untitled
                                        flat: true
                                        text: qsTr("Choose file…")
                                        onClicked: unsaved.pick(row.index)
                                    }
                                }
                            }
                            SegmentedButtons {
                                objectName: "closeChoice" + row.index
                                choices: [{ text: qsTr("Save"), name: "closeSave" + row.index },
                                          { text: qsTr("Don’t save"), name: "closeDiscard" + row.index }]
                                currentIndex: unsaved.choiceAt(row.index).save ? 0 : 1
                                Accessible.name: qsTr("Save %1").arg(row.modelData.title)
                                onActivated: index => unsaved.setSave(row.index, index === 0)
                            }
                        }
                    }
                }
            }
            Label {
                objectName: "closeReviewCount"
                text: qsTr("Saves %1 · Discards %2").arg(unsaved.saving).arg(unsaved.rows.length - unsaved.saving)
                color: Theme.muted
                font: reviewWindow.fontAt(12)
            }
            Label {
                objectName: "closeReviewProblem"
                text: unsaved.problem
                visible: text.length > 0
                color: Theme.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            leadingActions: [
                DangerButton {
                    objectName: "closeWithoutSaving"
                    text: reviewWindow.w.discard
                    onClicked: unsaved.discardAll()
                }
            ]
            buttons: [
                PrimaryButton {
                    id: saveButton
                    objectName: "closeSaveAndClose"
                    text: reviewWindow.w.save
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: unsaved.proceed()
                },
                Button {
                    objectName: "closeCancel"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: reviewWindow.reject()
                }
            ]
            defaultButton: saveButton
        }
    }
}
