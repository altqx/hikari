// Y6: the font catalog choice, Add, Manage and the Filter toggle of the font
// dialog (FontDialog "Filtering and font catalogs", FontDialog.cpp:422-486)
// and of the Style editor (StyleChange.cpp:78-130) at 20d647c4. `fonts` is
// the list ChangeCatalog made; the owner shows it and selects in it when
// listMade() is signalled.
//
// The two windows differ where legacy did: the font dialog reads
// STYLE_EDIT_FILTER_TEXT each time, keeps its choice within the list when
// the catalogs change and its "Without catalog" removes the first font per
// catalog entry (FontList::FindString answers 0); the Style editor reads the
// filter text when it opens, removes each catalog font itself and asks the
// catalog window to select its font when it is opened again.
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
    signal listMade()

    // The window opens: the catalogs and the two entries, "All fonts" chosen,
    // the Filter as STYLE_EDIT_FILTER_TEXT_ON says.
    function reset() {
        listed = true
        filterTextAtOpen = catalogs.filterText
        choices = catalogs.catalogChoices()
        choice.currentIndex = 0
        filter.checked = catalogs.filterOn
        changeCatalog(false)
    }
    // ChangeCatalog(save): the list for the choice; the Filter toggle saves
    // STYLE_EDIT_FILTER_TEXT_ON.
    function changeCatalog(save) {
        if (save)
            catalogs.setFilterOn(filter.checked)
        const value = choice.currentIndex >= 0 && choice.currentIndex < choices.length ? choices[choice.currentIndex] : ""
        fonts = catalogs.fontList(choice.currentIndex, value, filter.checked,
                                  fontDialog ? catalogs.filterText : filterTextAtOpen, fontDialog)
        listMade()
    }
    // CATALOG_CHANGED (the catalog window shown or hidden): the choice made
    // again around its selection, the list, then SaveCatalogs.
    function catalogsChangedByWindow() {
        let sel = choice.currentIndex
        choices = catalogs.catalogChoices()
        if (fontDialog && sel >= choices.length)
            sel = choices.length - 1
        // HikariChoice::SetSelection ignores a position past the end.
        if (sel < choices.length)
            choice.currentIndex = sel
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
    // ShowGetFromAssDialog's answer: the choice gets the catalog names alone
    // (PutArray without the two entries) with the catalog chosen.
    function collectedInto(catalog) {
        if (catalog.length === 0)
            return
        choices = catalogs.catalogNames
        const lower = catalog.toLowerCase()
        choice.currentIndex = choices.findIndex(n => n.toLowerCase() === lower)
        changeCatalog(false)
    }

    ComboBox {
        id: choice
        objectName: "fontCatalogChoice"
        Layout.fillWidth: true
        model: bar.choices
        Accessible.name: qsTr("Font catalogs")
        onActivated: bar.changeCatalog(false)
    }
    Button {
        id: addButton
        objectName: "fontCatalogAdd"
        text: qsTr("Add")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Adds fonts to a previously created catalog")
        onClicked: bar.addToCatalog()
    }
    Button {
        objectName: "fontCatalogManage"
        text: qsTr("Manage")
        ToolTip.visible: hovered
        ToolTip.text: bar.fontDialog ? qsTr("Manages font catalogs") : qsTr("Allows managing font catalogs")
        onClicked: {
            if (!manageWindow.created)
                manageWindow.create(bar.fontName)
            else if (!bar.fontDialog)
                manageWindow.setStyleFont(bar.fontName) // FCL->SetStyleFont
            manageWindow.open()
        }
    }
    Button {
        id: filter
        objectName: "fontFilter"
        text: qsTr("Filter")
        checkable: true
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Filters fonts to those containing the entered characters")
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
