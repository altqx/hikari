pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "fontsFromSubtitlesDialog"
    required property var catalogs
    property var notices: null
    property int openFiles: 1
    property string fileName: ""
    property var done: null
    title: qsTr("Add fonts from subtitles")
    dialogId: "fontsFromSubtitles"
    modal: true
    initialFocus: catalog

    function openFor(name, callback, opener) {
        // A shown window keeps its transient parent: close it to be modal to the new opener.
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
    // MUST KILL: target skips the catalog lookup while hidden so it does not load the catalogs
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
            catalogs.addCatalog(name, false)
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
            // Fusion's delegate would read markup in a catalog name from a file.
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
