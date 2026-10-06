// Y6: legacy GetFontsFromASSDialog "Add fonts from subtitles"
// (FontCatalogList.cpp:840-905 at 20d647c4): the catalog choice with Add,
// "Remove all contents of catalog", "Add fonts from all open subtitles", OK
// and Cancel. OK collects every Style's font and each \fn of the active
// tab's Lines (or every tab's) into the chosen catalog and saves the
// catalogs (CollectFontsFromSubtitles).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "fontsFromSubtitlesDialog"
    required property var catalogs
    title: qsTr("Add fonts from subtitles")
    modal: true
    // The editable choice's selection: SetSelection(0) at the start, a pick
    // from the list, and -1 once its text is typed (SetSelectionByPartialName).
    property int selection: -1
    signal collected(string catalog)

    function openFor(names) {
        catalog.model = names
        catalog.currentIndex = names.length > 0 ? 0 : -1
        catalog.editText = names.length > 0 ? names[0] : ""
        selection = names.length > 0 ? 0 : -1
        emptyCatalog.checked = false
        allSubs.checked = false
        open()
    }

    contentItem: ColumnLayout {
        RowLayout {
            Label { text: qsTr("Catalogs:") }
            ComboBox {
                id: catalog
                objectName: "fontsFromSubtitlesCatalog"
                editable: true
                Layout.fillWidth: true
                Layout.preferredWidth: 220
                Accessible.name: qsTr("Catalogs:")
                onActivated: index => dialog.selection = index
                Connections {
                    target: catalog.contentItem
                    ignoreUnknownSignals: true
                    function onTextEdited() { dialog.selection = -1 }
                }
            }
            Button {
                objectName: "fontsFromSubtitlesAdd"
                text: qsTr("Add")
                // AddCatalog(ctlg, nullptr, false) and Append: no autosave.
                onClicked: {
                    const name = catalog.editText
                    if (name.length > 0) {
                        dialog.catalogs.addCatalog(name, false)
                        catalog.model = catalog.model.concat([name])
                        catalog.editText = name
                    }
                }
            }
        }
        CheckBox { id: emptyCatalog; objectName: "fontsFromSubtitlesClear"; text: qsTr("Remove all contents of catalog") }
        CheckBox { id: allSubs; objectName: "fontsFromSubtitlesAllTabs"; text: qsTr("Add fonts from all open subtitles") }
    }
    footer: DialogButtonBox {
        Button { objectName: "fontsFromSubtitlesOk"; text: "OK"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    // ShowGetFromAssDialog: nothing without a selection; the editable
    // choice's text names the catalog (HikariChoice::GetString).
    onAccepted: {
        if (selection < 0)
            return
        const name = catalog.editText
        catalogs.collectFromSubtitles(name, emptyCatalog.checked, allSubs.checked)
        collected(name)
    }
}
