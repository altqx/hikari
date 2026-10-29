pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: bar
    objectName: "styleEditorCatalogBar"
    required property var catalogs
    property string fontName: ""
    property var fonts: []
    property var choices: []
    property string filterTextAtOpen: ""
    property bool listed: false
    // MUST KILL: staleText emulates legacy's choice text going stale past the list's new end
    property string staleText: ""
    signal listMade()

    function reset() {
        listed = true
        filterTextAtOpen = catalogs.filterText
        choices = catalogs.catalogChoices()
        choice.currentIndex = 0
        staleText = ""
        filter.checked = catalogs.filterOn
        changeCatalog(false)
    }
    function changeCatalog(save) {
        if (save)
            catalogs.setFilterOn(filter.checked)
        const value = staleText !== "" ? staleText
                    : choice.currentIndex >= 0 && choice.currentIndex < choices.length ? choices[choice.currentIndex] : ""
        fonts = catalogs.fontList(choice.currentIndex, value, filter.checked, filterTextAtOpen)
        listMade()
    }
    function catalogsChangedByWindow() {
        let sel = choice.currentIndex
        const before = choices
        choices = catalogs.catalogChoices()
        if (sel < choices.length) {
            choice.currentIndex = sel
            staleText = ""
        } else {
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
                    c = Math.max(0, names.indexOf(ce))
                    text = names[c]
                }
            }
            choice.currentIndex = c
            staleText = c >= 0 && c < choices.length && choices[c] === text ? "" : text
        }
        changeCatalog(false)
        catalogs.save()
    }
    function addToCatalog() {
        addMenu.targetFont = fontName
        addMenu.names = catalogs.catalogNames
        addMenu.popup(addButton, 0, addButton.height)
    }
    function collectedInto(catalog) {
        if (catalog.length === 0)
            return
        choices = catalogs.catalogChoices()
        const lower = catalog.toLowerCase()
        const at = catalogs.catalogNames.findIndex(n => n.toLowerCase() === lower)
        choice.currentIndex = at < 0 ? -1 : at + 2
        staleText = ""
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
        tip: qsTr("Allows managing font catalogs")
        onClicked: if (FontWindows.catalogWindow) FontWindows.catalogWindow.openFor(bar.fontName, bar.Window.window)
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

    Connections {
        target: bar.catalogs
        function onFontsChanged() {
            if (bar.listed)
                bar.changeCatalog(false)
        }
    }

    ShellMenu {
        id: addMenu
        objectName: "styleEditorCatalogAddMenu"
        property string targetFont: ""
        property var names: []
        ShellMenuItem {
            iconRole: "fonts-from-subtitles"
            objectName: "styleEditorFromSubtitlesItem"
            text: qsTr("Add fonts from subtitles…")
            onTriggered: if (FontWindows.fromSubtitles)
                             FontWindows.fromSubtitles.openFor("", catalog => bar.collectedInto(catalog), bar.Window.window)
        }
        ShellMenuItem {
            objectName: "styleEditorNoCatalogs"
            text: qsTr("No catalogs yet")
            enabled: false
            visible: addMenu.names.length === 0
            height: visible ? implicitHeight : 0
        }
        ShellMenuItem {
            objectName: "styleEditorNewCatalog"
            iconRole: "add"
            text: qsTr("New catalog…")
            visible: addMenu.names.length === 0
            height: visible ? implicitHeight : 0
            onTriggered: if (FontWindows.catalogWindow) FontWindows.catalogWindow.startNew(bar.fontName, bar.Window.window)
        }
        Instantiator {
            model: addMenu.names
            delegate: ShellMenuItem {
                required property string modelData
                objectName: "fontCatalogAddItem"
                text: modelData
                checkable: true
                checked: bar.catalogs.isFontInCatalog(modelData, addMenu.targetFont)
                keepsMenuOpen: false
                onTriggered: bar.catalogs.toggleFontInCatalog(modelData, addMenu.targetFont, checked)
            }
            onObjectAdded: (index, object) => addMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => addMenu.removeItem(object)
        }
    }
    Connections {
        target: FontWindows.catalogWindow
        ignoreUnknownSignals: true
        function onCatalogChanged() {
            if (bar.listed)
                bar.catalogsChangedByWindow()
        }
    }
}
