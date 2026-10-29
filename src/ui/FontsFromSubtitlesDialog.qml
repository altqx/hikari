// Y6: legacy GetFontsFromASSDialog "Add fonts from subtitles"
// (FontCatalogList.cpp:840-905 at 20d647c4). Add fonts collects every
// Style's font and each \fn of the active tab's Lines (or every tab's) into
// the chosen catalog and saves the catalogs (CollectFontsFromSubtitles).
//
// PU3 (#305): a compact window on DG1's frame, modal to its opener, one for
// the program (FontWindows). The catalog is chosen or typed, a new name
// making it (PU3-from-subtitles: legacy's Add button did, and OK without a
// chosen entry did nothing); "From" (This file / All open files) and
// "Replace the catalog's fonts" replace legacy's two check boxes, a hint
// says what will happen, "Add fonts" replaces OK, and the result is a
// toast.
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "fontsFromSubtitlesDialog"
    required property var catalogs
    property var notices: null // ToastStack
    // The open files, and the name of the one in use, for the hint.
    property int openFiles: 1
    property string fileName: ""
    property var done: null
    title: qsTr("Add fonts from subtitles")
    dialogId: "fontsFromSubtitles"
    modal: true
    initialFocus: catalog

    // Opens from `opener` (a window) with `name` chosen (a catalog, or the
    // first); done(catalog) once fonts were added.
    function openFor(name, callback, opener) {
        // Shown for another window: closed first, so that it is modal to the new one.
        if (visible && opener && transientParent !== opener && opener.transientParent !== dialog)
            close()
        const names = catalogs.catalogNames
        catalog.model = names
        const at = names.indexOf(name || "")
        catalog.currentIndex = at >= 0 ? at : names.length > 0 ? 0 : -1
        catalog.editText = catalog.currentIndex >= 0 ? names[catalog.currentIndex] : ""
        from.currentIndex = 0
        replace.checked = false
        done = callback || null
        openFrom(opener || null)
    }
    readonly property string chosen: catalog.editText.trim()
    // The catalog the name stands for, ignoring case, else the new name
    // (read while shown: the catalogs load at their first use).
    readonly property string target: {
        if (!visible)
            return chosen
        const existing = catalogs.existingName(chosen)
        return existing.length > 0 ? existing : chosen
    }
    function add() {
        const name = target
        if (name.length === 0)
            return
        const made = catalogs.existingName(name).length === 0
        if (made)
            catalogs.addCatalog(name, false) // the dialog's Add: no autosave
        const before = replace.checked ? 0 : catalogs.catalogSize(name)
        const result = catalogs.collectFromSubtitles(name, replace.checked, from.currentIndex === 1)
        if (!result.done)
            return
        const added = catalogs.catalogSize(name) - before
        const callback = done
        done = null
        accept()
        if (notices && !result.saved)
            notices.post("warning", qsTr("Add fonts from subtitles"),
                         qsTr("The fonts were added to “%1” but could not be saved.").arg(name),
                         { details: qsTr("fonts.toml in the \"catalogs\" folder of your user folder could not be written. Saving is tried again on exit.") })
        else if (notices)
            notices.post("info", qsTr("Add fonts from subtitles"),
                         (added === 1 ? qsTr("Added 1 font to “%1”") : qsTr("Added %n fonts to “%1”", "", added)).arg(name),
                         { details: from.currentIndex === 1 ? qsTr("From the styles and %1 tags of the open files.").arg("\\fn")
                                                            : qsTr("From the styles and %1 tags of %2.").arg("\\fn").arg(fileName) })
        if (callback)
            callback(name)
    }
    onRejected: done = null

    GridLayout {
        columns: 2
        columnSpacing: 10
        rowSpacing: 8
        Layout.topMargin: 8
        Label { text: qsTr("Catalog") }
        ComboBox {
            id: catalog
            objectName: "fontsFromSubtitlesCatalog"
            editable: true
            Layout.preferredWidth: 260
            Accessible.name: qsTr("Catalog")
            // The catalogs' names as plain text (Fusion's delegate would read
            // markup in a name from a loaded file).
            delegate: ItemDelegate {
                id: catalogItem
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData
                highlighted: catalog.highlightedIndex === index
                hoverEnabled: catalog.hoverEnabled
                contentItem: Label {
                    text: catalogItem.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    color: catalogItem.highlighted ? catalogItem.palette.highlightedText : catalogItem.palette.text
                }
            }
        }
        Item { width: 1; height: 1 }
        Label {
            textFormat: Text.PlainText
            text: qsTr("Choose a catalog or type a new name.")
            color: Theme.muted
            font: dialog.fontAt(12)
        }
        Label { text: qsTr("From") }
        SegmentedButtons {
            id: from
            objectName: "fontsFromSubtitlesFrom"
            choices: [{ text: qsTr("This file"), name: "fontsFromSubtitlesThisFile" },
                      { text: qsTr("All open files"), name: "fontsFromSubtitlesAllFiles" }]
            Accessible.name: qsTr("From")
            onActivated: index => currentIndex = index
        }
        Item { width: 1; height: 1 }
        CheckBox {
            id: replace
            objectName: "fontsFromSubtitlesReplace"
            text: qsTr("Replace the catalog’s fonts")
        }
        Item { width: 1; height: 1 }
        Label {
            textFormat: Text.PlainText
            objectName: "fontsFromSubtitlesHint"
            readonly property int size: dialog.visible ? dialog.catalogs.catalogSize(dialog.target) : 0
            text: (from.currentIndex === 1 && dialog.openFiles > 1
                       ? qsTr("The fonts of every style and every %1 tag in all %n open files.", "", dialog.openFiles).arg("\\fn")
                       : qsTr("The fonts of every style and every %1 tag in %2.").arg("\\fn").arg(dialog.fileName))
                  + (replace.checked && size > 0 ? " " + qsTr("The fonts “%1” holds now are removed first.").arg(dialog.target) : "")
            color: Theme.muted
            font: dialog.fontAt(12)
            wrapMode: Text.Wrap
            Layout.preferredWidth: 260
        }
    }

    buttons: [
        PrimaryButton {
            id: addButton
            objectName: "fontsFromSubtitlesOk"
            text: qsTr("Add fonts")
            enabled: dialog.chosen.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.add()
        },
        Button {
            objectName: "fontsFromSubtitlesCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
    defaultButton: addButton
}
