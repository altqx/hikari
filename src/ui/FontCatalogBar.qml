// Y6: the font catalog choice, Add, Manage and the Filter toggle of the font
// dialog (FontDialog "Filtering and font catalogs", FontDialog.cpp:422-486)
// and of the Style editor (StyleChange.cpp:78-130) at 20d647c4. `fonts` is
// the list ChangeCatalog made; the owner shows it and selects in it when
// listMade() is signalled.
//
// The two windows differ where legacy did: the font dialog reads
// STYLE_EDIT_FILTER_TEXT each time and keeps its choice within the list when
// the catalogs change; the Style editor reads the filter text when it opens
// and asks the catalog window to select its font when it is opened again.
// "Without catalog" removes the catalogs' fonts in both (Y6-without-catalog:
// legacy's font dialog removed its first font per catalog entry).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: bar
    objectName: fontDialog ? "fontDialogCatalogBar" : "styleEditorCatalogBar"
    required property var catalogs // FontCatalogsController
    property bool fontDialog: false
    // GetFontName / styleFont->GetValue(): the font Add and Manage act on.
    property string fontName: ""
    property var fonts: []
    property var choices: []
    // StyleChange reads STYLE_EDIT_FILTER_TEXT once, when it is made.
    property string filterTextAtOpen: ""
    property bool listed: false // the window made its list (a FontEnum observer)
    // HikariChoice's txtchoice where it no longer names the entry at the
    // choice (the Style editor after the catalogs changed under a choice past
    // the new end); "" while they agree. It is drawn and it is GetValue().
    property string staleText: ""
    signal listMade()

    // The window opens: the catalogs and the two entries, "All fonts" chosen,
    // the Filter as STYLE_EDIT_FILTER_TEXT_ON says.
    function reset() {
        listed = true
        filterTextAtOpen = catalogs.filterText
        choices = catalogs.catalogChoices()
        choice.currentIndex = 0
        staleText = ""
        filter.checked = catalogs.filterOn
        changeCatalog(false)
    }
    // ChangeCatalog(save): the list for the choice; the Filter toggle saves
    // STYLE_EDIT_FILTER_TEXT_ON.
    function changeCatalog(save) {
        if (save)
            catalogs.setFilterOn(filter.checked)
        const value = staleText !== "" ? staleText
                    : choice.currentIndex >= 0 && choice.currentIndex < choices.length ? choices[choice.currentIndex] : ""
        fonts = catalogs.fontList(choice.currentIndex, value, filter.checked,
                                  fontDialog ? catalogs.filterText : filterTextAtOpen)
        listMade()
    }
    // CATALOG_CHANGED (the catalog window shown or hidden): the choice made
    // again around its selection, the list, then SaveCatalogs.
    // The font dialog clamps the choice to the last entry (FontDialog.cpp:
    // 466-474); the Style editor does not (StyleChange.cpp:98-104).
    function catalogsChangedByWindow() {
        let sel = choice.currentIndex
        const before = choices
        choices = catalogs.catalogChoices()
        if (fontDialog && sel >= choices.length)
            sel = choices.length - 1
        if (sel < choices.length) {
            choice.currentIndex = sel // SetSelection(sel): its text follows
            staleText = ""
        } else {
            // HikariChoice::SetSelection ignores a position past the end
            // (ListControls.cpp:459), so the choice stays where
            // PutArray(names) left it before "All fonts" and "Without
            // catalog" were inserted ahead of it (ListControls.cpp:498-519,
            // 685-688), and its text stays that name.
            const names = catalogs.catalogNames
            const ce = sel >= 0 && sel < before.length ? before[sel] : ""
            let c = sel
            let text = staleText !== "" ? staleText : ce
            if (names.length < 1) {
                c = -1
            } else if (ce !== "") {
                if (c >= names.length) {
                    c = 0
                    text = names[0]
                }
                if (ce !== names[c]) {
                    c = Math.max(0, names.indexOf(ce)) // wxArrayString::Index
                    text = names[c]
                }
            }
            choice.currentIndex = c
            staleText = c >= 0 && c < choices.length && choices[c] === text ? "" : text
        }
        changeCatalog(false)
        catalogs.save()
    }
    // AddToCatalog: with no catalog legacy logged where to make one; else the
    // menu of "Add fonts from subtitles" and a check per catalog.
    function addToCatalog() {
        if (catalogs.catalogNames.length === 0) {
            catalogs.noCatalogToAddTo()
            return
        }
        addMenu.targetFont = fontName
        addMenu.names = catalogs.catalogNames
        addMenu.popup(addButton, 0, addButton.height)
    }
    // ShowGetFromAssDialog's answer: the choice made again with "All fonts"
    // and "Without catalog" and the catalog chosen (Y6-collect-choice:
    // legacy's PutArray left the catalog names alone, so the catalog's index
    // among them read as "All fonts" or "Without catalog" to ChangeCatalog).
    function collectedInto(catalog) {
        if (catalog.length === 0)
            return
        choices = catalogs.catalogChoices()
        const lower = catalog.toLowerCase()
        const at = catalogs.catalogNames.findIndex(n => n.toLowerCase() === lower)
        // The names follow the two entries (there is a catalog, so no clamp).
        choice.currentIndex = at < 0 ? -1 : at + 2
        staleText = "" // SetSelection
        changeCatalog(false)
    }

    ComboBox {
        id: choice
        objectName: "fontCatalogChoice"
        Layout.fillWidth: true
        model: bar.choices
        displayText: bar.staleText !== "" ? bar.staleText : currentText
        Accessible.name: qsTr("Font catalogs")
        onActivated: {
            bar.staleText = ""
            bar.changeCatalog(false)
        }
    }
    // An inline tool row beside the choice: icon-only, the help text as
    // the tooltip and the name as the accessible name.
    IconToolButton {
        id: addButton
        objectName: "fontCatalogAdd"
        iconRole: "add"
        text: qsTr("Add")
        tip: qsTr("Adds fonts to a previously created catalog")
        onClicked: bar.addToCatalog()
    }
    IconToolButton {
        objectName: "fontCatalogManage"
        iconRole: "settings"
        text: qsTr("Manage")
        tip: bar.fontDialog ? qsTr("Manages font catalogs") : qsTr("Allows managing font catalogs")
        onClicked: {
            if (!manageWindow.created)
                manageWindow.create(bar.fontName)
            else if (!bar.fontDialog)
                manageWindow.setStyleFont(bar.fontName) // FCL->SetStyleFont
            manageWindow.open()
        }
    }
    IconToolButton {
        id: filter
        objectName: "fontFilter"
        iconRole: "filter"
        text: qsTr("Filter")
        checkable: true
        tip: qsTr("Filters fonts to those containing the entered characters")
        onToggled: bar.changeCatalog(true)
    }

    // FontEnum's observers: a font change lists the fonts again (ReloadFonts).
    Connections {
        target: bar.catalogs
        function onFontsChanged() {
            if (bar.listed)
                bar.changeCatalog(false)
        }
    }

    ShellMenu {
        id: addMenu
        objectName: bar.fontDialog ? "fontDialogCatalogAddMenu" : "styleEditorCatalogAddMenu"
        property string targetFont: ""
        property var names: []
        ShellMenuItem {
            objectName: "fontsFromSubtitlesItem"
            text: qsTr("Add fonts from subtitles")
            onTriggered: fromSubtitles.openFor(bar.catalogs.catalogNames)
        }
        Instantiator {
            model: addMenu.names
            delegate: ShellMenuItem {
                required property string modelData
                objectName: "fontCatalogAddItem"
                text: modelData
                checkable: true
                checked: bar.catalogs.isFontInCatalog(modelData, addMenu.targetFont)
                onTriggered: bar.catalogs.toggleFontInCatalog(modelData, addMenu.targetFont, checked)
            }
            onObjectAdded: (index, object) => addMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => addMenu.removeItem(object)
        }
    }
    FontsFromSubtitlesDialog {
        id: fromSubtitles
        objectName: bar.fontDialog ? "fontDialogFromSubtitles" : "styleEditorFromSubtitles"
        catalogs: bar.catalogs
        parent: Overlay.overlay
        anchors.centerIn: parent
        onCollected: catalog => bar.collectedInto(catalog)
    }
    FontCatalogWindow {
        id: manageWindow
        objectName: bar.fontDialog ? "fontDialogCatalogWindow" : "styleEditorCatalogWindow"
        catalogs: bar.catalogs
        parent: Overlay.overlay
        anchors.centerIn: parent
        onCatalogChanged: bar.catalogsChangedByWindow()
    }
}
