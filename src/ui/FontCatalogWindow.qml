// Y6: legacy FontCatalogList "Manage font catalogs" (FontCatalogList.cpp:
// 94-360 at 20d647c4), a modeless resizable window: the catalog choice with
// Add, Edit, Delete and Load; Find, the filter text and "Save filter"; every
// installed font with its mark, its catalog (a menu of the catalogs) and a
// sample; the preview; the status bar. Edits are autosaved to
// FontCatalogsAutosave0..2.txt 20 s after the first, and FontCatalogs.txt is
// written when the window is shown or hidden (CATALOG_CHANGED) and, once a
// window was made, when the application ends (~FontCatalogList).
//
// "Refresh fonts" (the card's explicit refresh, F47-refresh) reads the
// installed and external fonts again at once.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: window
    objectName: "fontCatalogWindow"
    required property var catalogs // FontCatalogsController
    title: qsTr("Manage font catalogs")
    modal: false
    width: 760
    height: 640
    property bool created: false
    property var fonts: []        // GetFonts when the window was made
    property var rowCatalogs: []  // the Catalog column
    property var marked: []       // each row's mark (FontItem::modified)
    // CATALOG_CHANGED, sent when the window is shown and when it is hidden.
    signal catalogChanged()

    // GenerateList(styleFont): every font, its catalog, the Style's font
    // selected (the first equal ignoring case, else the first).
    function create(styleFont) {
        fonts = catalogs.allFonts()
        rowCatalogs = fonts.map(f => catalogs.catalogOf(f))
        marked = fonts.map(() => false)
        catalogField.model = catalogs.catalogNames
        catalogField.currentIndex = -1
        catalogField.editText = ""
        seek.text = ""
        filterField.text = catalogs.filterText
        created = true
        catalogs.windowMade() // ~FontCatalogList saves at the end
        select(catalogs.styleFontIndex(fonts, styleFont))
        refreshPreview()
    }
    // SetStyleFont: FindItem(0, styleFont), selected when listed.
    function setStyleFont(font) {
        const i = fonts.indexOf(font)
        if (i >= 0)
            select(i)
    }
    function select(row) {
        list.currentIndex = row
        list.positionViewAtIndex(Math.max(0, row - 2), ListView.Beginning) // ScrollTo(sel - 2)
    }
    // RefreshList: each row's catalog again, and the choice's names.
    function refreshList() {
        rowCatalogs = fonts.map(f => catalogs.catalogOf(f))
        const text = catalogField.editText
        catalogField.model = catalogs.catalogNames
        catalogField.editText = text
    }
    function refreshPreview() {
        if (list.currentIndex >= 0 && list.currentIndex < fonts.length)
            catalogs.renderPreview(fonts[list.currentIndex], Math.max(1, preview.width), Math.max(1, preview.height))
    }
    function findIndex(text) {
        // HikariChoice::FindString: ignoring case, -1 for an empty text.
        if (text.length === 0)
            return -1
        const lower = text.toLowerCase()
        return catalogField.model.findIndex(n => n.toLowerCase() === lower)
    }
    // CatalogList's menu chose `catalog` for the selected row: the row and
    // the marked rows join or leave it; their marks clear.
    function chooseCatalog(row, catalog, add) {
        const font = fonts[row]
        const others = []
        for (let i = 0; i < fonts.length; ++i)
            if (marked[i] && i !== row)
                others.push(i)
        catalogs.setFontsInCatalog(catalog, [font].concat(others.map(i => fonts[i])), add)
        const cats = rowCatalogs.slice()
        const marks = marked.slice()
        for (const i of others) {
            cats[i] = add ? catalog : ""
            marks[i] = false
        }
        marks[row] = false
        cats[row] = add ? catalog : catalogs.catalogOf(font)
        rowCatalogs = cats
        marked = marks
    }

    onOpened: catalogChanged()
    onClosed: catalogChanged()

    contentItem: ColumnLayout {
        RowLayout {
            Label { text: qsTr("Catalogs:") }
            ComboBox {
                id: catalogField
                objectName: "fontCatalogField"
                editable: true
                Layout.fillWidth: true
                Accessible.name: qsTr("Catalogs:")
            }
            Button {
                objectName: "fontCatalogAddCatalog"
                text: qsTr("Add")
                onClicked: {
                    const name = catalogField.editText
                    if (name.length > 0) {
                        window.catalogs.addCatalog(name, true)
                        catalogField.model = catalogField.model.concat([name]) // Append
                        catalogField.editText = name
                    }
                }
            }
            Button {
                objectName: "fontCatalogEdit"
                text: qsTr("Edit")
                onClicked: edition.openFor(window.findIndex(catalogField.editText))
            }
            Button {
                objectName: "fontCatalogDelete"
                text: qsTr("Delete")
                onClicked: {
                    const name = catalogField.editText
                    const index = window.findIndex(name)
                    if (name.length > 0 && index !== -1) {
                        deleteQuestion.name = name
                        deleteQuestion.index = index
                        deleteQuestion.open()
                    }
                }
            }
            Button {
                objectName: "fontCatalogLoad"
                text: qsTr("Load")
                onClicked: loadDialog.open()
            }
            Button {
                objectName: "fontCatalogRefreshFonts"
                text: qsTr("Refresh fonts")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Reads the installed and external fonts again")
                onClicked: {
                    window.catalogs.refreshFonts()
                    const font = list.currentIndex >= 0 ? window.fonts[list.currentIndex] : ""
                    window.create(font)
                }
            }
        }
        RowLayout {
            Label { text: qsTr("Find:") }
            TextField {
                id: seek
                objectName: "fontCatalogFind"
                Layout.fillWidth: true
                Accessible.name: qsTr("Find:")
                onTextEdited: window.select(window.catalogs.catalogListIndex(window.fonts, text))
            }
            TextField {
                id: filterField
                objectName: "fontCatalogFilterText"
                Layout.fillWidth: true
                Accessible.name: qsTr("Filter")
            }
            Button {
                objectName: "fontCatalogSaveFilter"
                text: qsTr("Save filter")
                onClicked: window.catalogs.saveFilterText(filterField.text)
            }
        }
        RowLayout {
            Label { text: qsTr("Font name"); Layout.preferredWidth: 290 }
            Label { text: qsTr("Catalog"); Layout.preferredWidth: 140 }
            Label {
                text: qsTr("Sample")
                Layout.fillWidth: true
                // fonts.md: a Qt-drawn sample is labelled as one.
                ToolTip.visible: sampleHover.hovered
                ToolTip.text: qsTr("Drawn by Qt, not by the subtitle renderer")
                HoverHandler { id: sampleHover }
            }
        }
        ListView {
            id: list
            objectName: "fontCatalogList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
            clip: true
            model: window.fonts
            currentIndex: -1
            highlightMoveDuration: 0
            Accessible.name: qsTr("Fonts")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property string modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: { // LIST_ITEM_LEFT_CLICK: the preview
                    list.currentIndex = index
                    window.refreshPreview()
                }
                contentItem: RowLayout {
                    CheckBox {
                        objectName: "fontCatalogMark"
                        checked: window.marked[row.index] === true
                        Accessible.name: qsTr("Mark %1").arg(row.modelData)
                        onToggled: {
                            const marks = window.marked.slice()
                            marks[row.index] = checked
                            window.marked = marks
                        }
                    }
                    Label { text: row.modelData; elide: Text.ElideRight; Layout.preferredWidth: 260 }
                    Button {
                        objectName: "fontCatalogCell"
                        text: window.rowCatalogs[row.index] || ""
                        enabled: true
                        Layout.preferredWidth: 140
                        Accessible.name: qsTr("Catalog of %1").arg(row.modelData)
                        onClicked: {
                            list.currentIndex = row.index
                            cellMenu.row = row.index
                            cellMenu.names = window.catalogs.catalogNames
                            cellMenu.popup(this, 0, height)
                        }
                    }
                    Label {
                        text: window.catalogs.previewText
                        font.family: row.modelData
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Image {
            id: preview
            objectName: "fontCatalogPreview"
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            cache: false
            fillMode: Image.Pad
            source: window.catalogs.previewKey > 0 ? "image://fontcatalogpreview/" + window.catalogs.previewKey : ""
        }
        RowLayout {
            Label { objectName: "fontCatalogStatus"; text: window.catalogs.autosaveStatus; Layout.preferredWidth: 160 }
            Label { text: qsTr("The catalog list has autosave; files are stored in the \"Config\" folder."); Layout.fillWidth: true; elide: Text.ElideRight }
        }
    }

    // CatalogList::OnMouseEvent's menu: the catalogs, checked where the font is.
    ShellMenu {
        id: cellMenu
        objectName: "fontCatalogCellMenu"
        property int row: -1
        property var names: []
        Instantiator {
            model: cellMenu.names
            delegate: ShellMenuItem {
                required property string modelData
                text: modelData
                checkable: true
                checked: cellMenu.row >= 0 && window.catalogs.isFontInCatalog(modelData, window.fonts[cellMenu.row])
                onTriggered: window.chooseCatalog(cellMenu.row, modelData, checked)
            }
            onObjectAdded: (index, object) => cellMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => cellMenu.removeItem(object)
        }
    }

    // CatalogEdition "Select the name of the profile".
    Dialog {
        id: edition
        objectName: "catalogEditionDialog"
        title: qsTr("Select the name of the profile")
        modal: true
        anchors.centerIn: parent
        function openFor(index) {
            current.model = window.catalogs.catalogNames
            current.currentIndex = index !== -1 ? index : 0
            replacement.text = ""
            open()
        }
        contentItem: GroupBox {
            title: qsTr("Choose new catalog name")
            RowLayout {
                ComboBox { id: current; objectName: "catalogEditionCurrent"; Layout.fillWidth: true; Accessible.name: qsTr("Catalog") }
                Label { text: qsTr("Replace with:") }
                TextField { id: replacement; objectName: "catalogEditionNewName"; Layout.fillWidth: true; Accessible.name: qsTr("Replace with:") }
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "catalogEditionOk"
                text: "OK"
                onClicked: {
                    if (replacement.text.length === 0) {
                        info.open() // "Enter a name for the new catalog"
                        return
                    }
                    edition.close()
                    const oldName = current.currentIndex >= 0 ? current.currentText : ""
                    if (window.catalogs.catalogExists(replacement.text)) {
                        clash.oldName = oldName
                        clash.newName = replacement.text
                        clash.open()
                    } else {
                        window.renamed(oldName, replacement.text, 0)
                    }
                }
            }
            Button { text: qsTr("Cancel"); onClicked: edition.close() }
        }
    }
    function renamed(oldName, newName, answer) {
        if (catalogs.renameCatalog(oldName, newName, answer)) {
            refreshList()
            catalogField.editText = newName // catalog->SetValue
        }
    }
    Dialog {
        id: info
        title: qsTr("Info")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { text: qsTr("Enter a name for the new catalog") }
        onClosed: edition.open()
    }
    // ChangeCatalogName's question: Merge (Yes), Delete (No), Cancel.
    Dialog {
        id: clash
        objectName: "catalogClashQuestion"
        property string oldName
        property string newName
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        Label { text: qsTr("Catalog named \"%1\" already exists. What to do?").arg(clash.newName) }
        footer: DialogButtonBox {
            Button { objectName: "catalogClashMerge"; text: qsTr("Merge"); onClicked: { clash.close(); window.renamed(clash.oldName, clash.newName, 0) } }
            Button { objectName: "catalogClashDelete"; text: qsTr("Delete"); onClicked: { clash.close(); window.renamed(clash.oldName, clash.newName, 1) } }
            Button { text: qsTr("Cancel"); onClicked: clash.close() }
        }
    }
    Dialog {
        id: deleteQuestion
        objectName: "catalogDeleteQuestion"
        property string name
        property int index: -1
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Are you sure you want to delete this catalog?") }
        onAccepted: {
            window.catalogs.removeCatalog(name)
            const names = catalogField.model.slice()
            names.splice(index, 1) // catalog->Delete(index)
            catalogField.model = names
            catalogField.editText = "" // SetValue("")
        }
    }
    // OnLoadCatalogs (FontCatalogList.cpp:315-316): legacy's chooser title
    // (sic) and "Text files (*.txt)", whose pattern Qt reads from the label.
    Dialogs.FileDialog {
        id: loadDialog
        title: qsTr("Choose video file")
        nameFilters: [qsTr("Text files (*.txt)")]
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: {
            window.catalogs.loadCatalogsFrom(selectedFile)
            window.refreshList()
        }
    }
}
