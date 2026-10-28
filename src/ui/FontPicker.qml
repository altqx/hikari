pragma ComponentBehavior: Bound
// PU3 (#305): the font picker, one window on DG1's frame (it replaces E1's
// FontDialog, legacy FontDialog "Select a font"), titled for the command
// that opens it, "Font selection":
// * a search field that filters the families as you type (Ctrl+F; Up and
//   Down move in the list while the field keeps the keyboard, Enter is
//   OK). A name no font answers to can still be used ("Use “…” anyway"),
//   for a file made for another machine (PU3-font-search: legacy's name
//   field selected by prefix, FontList::SetSelectionByPartialName);
// * the catalog as a chip (All fonts, Without catalog, a catalog; Y6's
//   catalog choice, FontDialog.cpp:656-695) and the characters filter as a
//   second chip, its text in a popover (legacy's Filter toggle and the
//   catalog window's "Save filter", PU3-character-chip);
// * each family drawn in its own face (SY3's rows), its catalogs muted at
//   the trailing edge; + adds the family to a catalog (legacy's Add menu);
// * the size and the Bold, Italic, Underline and Strikethrough toggles;
// * the preview: the Line's text, its selected part if there is one, drawn
//   by the subtitle renderer, and the face it selected for the name
//   (PU3-renderer-preview: legacy drew "AaBbCc…" with the system's font);
// * "Manage catalogs…" opens the catalog window, a window of its own.
//
// From the Line editor (openFor) each change is applied to the edited text
// at once, as legacy's FONT_CHANGED does, and Cancel takes every change
// back. From the Style editor (choose) the preview is the Style manager's
// sample text and OK hands the font to the opener.
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: picker
    objectName: "fontPicker"
    required property var catalogs // FontCatalogsController
    property LineEditorController editor: null
    title: qsTr("Font selection")
    dialogId: "fontPicker"
    resizable: true
    modal: true
    initialFocus: search

    // What it opened for: "line" (the Line editor) or "style" (choose()).
    property string mode: "line"
    property var done: null
    property var openerWindow: null // choose()'s opener
    property bool loading: false
    property string family: ""
    // The preview's text and where it comes from: "selection", "line" or "sample".
    property string sampleText: ""
    property string sampleSource: "sample"
    property bool sampleAss: true // the sample is ASS text (override blocks are not drawn)
    // The catalog choice: "All fonts", "Without catalog", then the catalogs.
    property var choices: []
    property int choice: 0
    property var listed: [] // ChangeCatalog's list for the choice and the characters
    property var installed: [] // every installed font
    readonly property int total: installed.length
    property var resolution: ({})
    property int resolveRequest: -1
    property string resolvedFor: "" // the characters the last lookup asked for

    readonly property var shown: {
        const term = search.text.trim().toLowerCase()
        return term.length === 0 ? listed : listed.filter(f => f.toLowerCase().indexOf(term) >= 0)
    }
    readonly property int familyIndex: indexIn(shown, family)
    // The search text names no installed family: it can be used as it is.
    readonly property string unlisted: {
        const term = search.text.trim()
        return term.length > 0 && indexIn(shown, term) < 0 && indexIn(installed, term) < 0 ? term : ""
    }
    // The search text names an installed family the catalog or the
    // characters leave out.
    readonly property string leftOut: {
        const term = search.text.trim()
        return term.length > 0 && indexIn(shown, term) < 0 && indexIn(installed, term) >= 0 ? term : ""
    }
    readonly property bool catalogChosen: choice > 1

    // Opens on the Line editor's field `role` with its selection; false
    // when the editor refuses. The preview draws what the selection (else
    // the line) draws, from the raw text.
    function openFor(role, selectionStart, selectionEnd) {
        const f = editor.beginFont(role, selectionStart, selectionEnd)
        if (f.name === undefined)
            return false
        mode = "line"
        done = null
        openerWindow = null
        const raw = f.sample || ""
        const selected = f.sampleTo > f.sampleFrom ? catalogs.samplePlain(raw, editor.assFields, f.sampleFrom, f.sampleTo) : ""
        const line = catalogs.samplePlain(raw, editor.assFields)
        sampleSource = selected.length > 0 ? "selection" : line.length > 0 ? "line" : "sample"
        sampleText = selected.length > 0 ? selected : line
        sampleAss = false // plain already
        start(f)
        return true
    }
    // Opens with `font` ({name, size, bold, italic, underline, strikeOut})
    // and the sample text, from `opener` (the Style editor's window; null:
    // the one with the keyboard); done(font) on OK.
    function choose(font, sample, callback, opener) {
        mode = "style"
        done = callback || null
        openerWindow = opener || null
        sampleSource = "sample"
        sampleText = sample || ""
        sampleAss = true
        start(font)
    }
    function start(f) {
        loading = true
        family = f.name || ""
        sizeField.text = f.size !== undefined ? String(f.size) : ""
        bold.checked = f.bold === true
        italic.checked = f.italic === true
        underline.checked = f.underline === true
        strikeOut.checked = f.strikeOut === true
        search.text = ""
        choices = catalogChoices()
        choice = 0
        relist()
        loading = false
        resolve()
        openFrom(openerWindow)
        showFamily()
        renderTimer.restart()
    }
    function current() {
        return { name: family, size: sizeField.text, bold: bold.checked, italic: italic.checked,
                 underline: underline.checked, strikeOut: strikeOut.checked }
    }
    // "All fonts" then "Without catalog", then the catalogs (with none,
    // legacy's clamped insert puts "Without catalog" first).
    function catalogChoices() {
        const c = catalogs.catalogChoices()
        return c.length === 2 && catalogs.catalogNames.length === 0 ? [c[1], c[0]] : c
    }
    // The same family ignoring case as the catalogs compare names.
    function indexIn(list, name) {
        return catalogs.sameNameIndex(list, name)
    }
    // ChangeCatalog: the list for the choice and the characters filter.
    function relist() {
        const value = choice >= 0 && choice < choices.length ? choices[choice] : ""
        listed = catalogs.fontList(choice, value, catalogs.filterOn, catalogs.filterText)
        installed = catalogs.allFonts()
    }
    function chooseCatalog(index) {
        choice = index
        relist()
        showFamily()
    }
    function showFamily() {
        if (familyIndex >= 0)
            list.positionViewAtIndex(familyIndex, ListView.Contain)
    }
    // A family chosen in the list or typed.
    function pick(name) {
        if (name === family)
            return
        family = name
        changed()
        showFamily()
    }
    // Typing picks the best family shown: the one of that name, else the
    // first starting with it, else the first.
    function pickTyped() {
        const term = search.text.trim().toLowerCase()
        if (shown.length === 0 || term.length === 0)
            return
        let at = indexIn(shown, term)
        if (at < 0)
            at = shown.findIndex(f => f.toLowerCase().startsWith(term))
        pick(shown[Math.max(0, at)])
    }
    function step(by) {
        if (shown.length === 0)
            return
        const at = familyIndex < 0 ? 0 : Math.max(0, Math.min(shown.length - 1, familyIndex + by))
        pick(shown[at])
    }
    function changed() {
        if (loading)
            return
        if (mode === "line")
            changeTimer.restart()
        resolve()
        renderTimer.restart()
    }
    // Y6: what the renderer selects for the name (requested and resolved
    // identities stay distinct, fonts.md).
    function resolve() {
        resolution = ({}) // until this request's answer: no other face is said
        // The characters the preview draws need the glyphs (a CJK Line, a
        // CJK face): the Line's, else the preview text's.
        const own = sampleText.trim().length > 0
        resolvedFor = own ? catalogs.samplePlain(sampleText, sampleAss) : catalogs.samplePlain(catalogs.previewText, true)
        resolveRequest = catalogs.resolveFamily(family, bold.checked, italic.checked, true, resolvedFor)
    }
    // SV1: drawn again on a screen of another scale, or when the setting
    // changes (Window.devicePixelRatio, Qt 6.11).
    readonly property real previewPixelRatio: preview.Window.window ? preview.Window.window.devicePixelRatio : 1
    onPreviewPixelRatioChanged: renderTimer.restart()
    Connections {
        target: picker.catalogs
        function onPreviewAtScreenSizeChanged() { renderTimer.restart() }
    }
    function render() {
        const own = sampleText.trim().length > 0
        catalogs.renderSample(current(), own ? sampleText : catalogs.previewText, !own || sampleAss,
                              Math.max(1, preview.width), Math.max(1, preview.height), previewPixelRatio)
    }
    function flush() {
        if (changeTimer.running) {
            changeTimer.stop()
            editor.changeFont(current())
        }
    }
    // The catalogs changed (the catalog window, + or Add fonts from
    // subtitles): the choice stays on its catalog while it exists.
    function catalogsEdited() {
        const was = choice >= 0 && choice < choices.length ? choices[choice] : ""
        choices = catalogChoices()
        const at = choice > 1 ? catalogs.sameNameIndex(choices.slice(2), was) : choice
        choice = at < 0 ? 0 : choice > 1 ? at + 2 : at
        relist()
    }

    Timer {
        id: changeTimer
        interval: 100
        onTriggered: picker.editor.changeFont(picker.current())
    }
    Timer {
        id: renderTimer
        interval: 60
        onTriggered: picker.render()
    }
    Connections {
        target: picker.catalogs
        function onResolutionReady(requestId, result) {
            if (requestId === picker.resolveRequest)
                picker.resolution = result
        }
        function onCatalogsChanged() { if (picker.visible) picker.catalogsEdited() }
        // A refresh can change which face the name gets: the note and the
        // preview follow it.
        function onFontsChanged() {
            if (!picker.visible)
                return
            picker.relist()
            picker.resolve()
            renderTimer.restart()
        }
        // The filter, or (PV1 #398) the preview text changed in Settings or
        // the Style editor: a sample without the Line's text follows it.
        function onFilterChanged() {
            if (!picker.visible)
                return
            picker.relist()
            if (!picker.loading && picker.sampleText.trim().length === 0
                    && picker.catalogs.samplePlain(picker.catalogs.previewText, true) !== picker.resolvedFor) {
                picker.resolve()
                renderTimer.restart()
            }
        }
    }
    onAccepted: {
        if (mode === "line") {
            flush()
            editor.endDialog(true)
        } else if (done) {
            const callback = done
            done = null
            callback(current())
        }
    }
    onRejected: {
        changeTimer.stop()
        if (mode === "line")
            editor.endDialog(false)
        done = null
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: picker.visible
        onActivated: search.forceActiveFocus()
    }

    // The search, the two chips and +.
    RowLayout {
        spacing: 6
        Layout.fillWidth: true
        Layout.topMargin: 6
        TextField {
            id: search
            objectName: "fontPickerSearch"
            placeholderText: qsTr("Search fonts")
            Accessible.name: qsTr("Search fonts")
            Layout.preferredWidth: 236
            leftPadding: 28
            onTextEdited: picker.pickTyped()
            onAccepted: picker.accept()
            Keys.onUpPressed: picker.step(-1)
            Keys.onDownPressed: picker.step(1)
            Icon {
                iconRole: "search"
                anchors.left: parent.left
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        FilterChip {
            id: catalogChip
            objectName: "fontPickerCatalog"
            iconRole: "styles"
            text: picker.choice >= 0 && picker.choice < picker.choices.length ? picker.choices[picker.choice] : ""
            on: picker.choice > 0
            Accessible.name: qsTr("Font catalog: %1").arg(text)
            onClicked: catalogMenu.openAt(catalogChip)
        }
        FilterChip {
            id: charactersChip
            objectName: "fontPickerCharacters"
            iconRole: "filter"
            on: picker.catalogs.filterOn && picker.catalogs.filterText.length > 0
            text: on ? qsTr("Has “%1”").arg(picker.catalogs.filterText) : qsTr("Characters")
            Accessible.name: on ? qsTr("Only fonts that have “%1”").arg(picker.catalogs.filterText)
                                : qsTr("Characters filter")
            onClicked: charactersPopover.openFor(charactersChip)
        }
        Item { Layout.fillWidth: true }
        IconToolButton {
            id: addButton
            objectName: "fontPickerAdd"
            iconRole: "add"
            text: qsTr("Add to a catalog")
            tip: qsTr("Add “%1” to a catalog").arg(picker.family)
            enabled: picker.family.length > 0
            onClicked: addMenu.openAt(addButton)
        }
    }

    RowLayout {
        spacing: 16
        Layout.fillWidth: true
        Layout.fillHeight: true
        Rectangle {
            color: Theme.field
            border.color: Theme.line
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 360
            Layout.preferredHeight: 280
            Layout.minimumHeight: 160
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                visible: picker.shown.length === 0
                Label {
                    textFormat: Text.PlainText
                    objectName: "fontPickerEmpty"
                    text: picker.unlisted.length > 0 ? qsTr("No installed font is called “%1”.").arg(picker.unlisted)
                        : picker.leftOut.length > 0 ? qsTr("“%1” is installed but not in this list.").arg(picker.leftOut)
                        : picker.catalogChosen ? qsTr("No installed font is in “%1”.").arg(picker.choices[picker.choice])
                        : qsTr("No fonts match")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.margins: 10
                }
                Item { Layout.fillHeight: true }
            }
            ListView {
                id: list
                objectName: "fontPickerList"
                anchors.fill: parent
                anchors.margins: 1
                anchors.topMargin: picker.shown.length === 0 ? 40 : 1
                clip: true
                model: picker.shown
                currentIndex: picker.familyIndex
                highlightMoveDuration: 0
                Accessible.name: qsTr("Fonts")
                ScrollBar.vertical: ScrollBar {}
                Keys.onUpPressed: picker.step(-1)
                Keys.onDownPressed: picker.step(1)
                delegate: FontFamilyRow {
                    id: familyRow
                    required property string modelData
                    required property int index
                    objectName: "fontPickerRow"
                    family: modelData
                    note: picker.catalogs.catalogsOf(modelData).join(", ")
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    onClicked: picker.pick(modelData)
                    onDoubleClicked: { picker.pick(modelData); picker.accept() }
                }
                footer: ItemDelegate {
                    objectName: "fontPickerUseUnlisted"
                    visible: picker.unlisted.length > 0
                    width: ListView.view.width
                    height: visible ? 28 : 0
                    highlighted: picker.family === picker.unlisted
                    text: qsTr("Use “%1” anyway").arg(picker.unlisted)
                    onClicked: picker.pick(picker.unlisted)
                    contentItem: Label { // the typed name as written, never markup
                        text: parent.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 150
            Label { text: qsTr("Size"); color: Theme.muted; font: picker.fontAt(11) }
            TextField {
                id: sizeField
                objectName: "fontPickerSize"
                Layout.preferredWidth: 96
                Accessible.name: qsTr("Font size")
                validator: DoubleValidator { bottom: 1; top: 10000; notation: DoubleValidator.StandardNotation }
                onTextChanged: if (acceptableInput) picker.changed()
            }
            Label { text: qsTr("Style"); color: Theme.muted; font: picker.fontAt(11); Layout.topMargin: 8 }
            RowLayout {
                spacing: 2
                IconToolButton { id: bold; objectName: "fontPickerBold"; iconRole: "tag-bold"; text: qsTr("Bold"); checkable: true; onToggled: picker.changed() }
                IconToolButton { id: italic; objectName: "fontPickerItalic"; iconRole: "tag-italic"; text: qsTr("Italic"); checkable: true; onToggled: picker.changed() }
                IconToolButton { id: underline; objectName: "fontPickerUnderline"; iconRole: "tag-underline"; text: qsTr("Underline"); checkable: true; onToggled: picker.changed() }
                IconToolButton { id: strikeOut; objectName: "fontPickerStrikeOut"; iconRole: "tag-strikeout"; text: qsTr("Strikethrough"); checkable: true; onToggled: picker.changed() }
            }
        }
    }
    Label {
        textFormat: Text.PlainText
        objectName: "fontPickerCount"
        text: picker.shown.length !== picker.total ? qsTr("%1 of %n fonts", "", picker.total).arg(picker.shown.length)
              : picker.total === 1 ? qsTr("1 font") : qsTr("%n fonts", "", picker.total)
        color: Theme.muted
        font: picker.fontAt(11)
        Layout.topMargin: -4
    }
    // The preview, by the subtitle renderer.
    Rectangle {
        color: Theme.field
        border.color: Theme.line
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        Layout.topMargin: 4
        Image {
            id: preview
            objectName: "fontPickerPreview"
            anchors.fill: parent
            anchors.margins: 1
            cache: false
            // SV1: drawn with the screen's pixels, shown at the preview's
            // size (Image ignores the image's pixel ratio).
            fillMode: picker.catalogs.sampleScale > 1 ? Image.PreserveAspectFit : Image.Pad
            source: picker.catalogs.sampleKey > 0 ? "image://fontcatalogpreview/sample/" + picker.catalogs.sampleKey : ""
            onWidthChanged: renderTimer.restart()
            onHeightChanged: renderTimer.restart()
        }
    }
    FontResolutionNote {
        objectName: "fontResolution"
        resolution: picker.resolution
        Layout.fillWidth: true
    }
    Label {
        textFormat: Text.PlainText
        objectName: "fontPickerSampleSource"
        text: picker.sampleText.trim().length === 0 ? qsTr("Sample text")
            : picker.sampleSource === "selection" ? qsTr("The line’s selected text")
            : picker.sampleSource === "line" ? qsTr("The line’s text")
            : qsTr("The Style editor’s sample text")
        color: Theme.muted
        font: picker.fontAt(11)
        Layout.topMargin: -4
    }

    leadingActions: [
        Button {
            objectName: "fontPickerManage"
            text: qsTr("Manage catalogs…")
            onClicked: if (FontWindows.catalogWindow) FontWindows.catalogWindow.openFor(picker.family, picker)
        }
    ]
    buttons: [
        PrimaryButton {
            id: okButton
            objectName: "fontPickerOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: picker.accept()
        },
        Button {
            objectName: "fontPickerCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: picker.reject()
        }
    ]
    defaultButton: okButton

    // The catalog chip's menu: the choices with their sizes, then Manage catalogs….
    ShellMenu {
        id: catalogMenu
        objectName: "fontPickerCatalogMenu"
        property var counts: []
        function openAt(anchor) {
            const n = picker.choices.length
            const out = []
            for (let i = 0; i < n; ++i)
                out.push(i < 2 ? picker.catalogs.fontList(i, picker.choices[i], false, "").length
                               : picker.catalogs.catalogSize(picker.choices[i]))
            counts = out
            popup(anchor, 0, anchor.height)
        }
        Instantiator {
            model: picker.choices
            delegate: ShellMenuItem {
                required property string modelData
                required property int index
                objectName: "fontPickerCatalogItem"
                text: modelData
                shortcutText: catalogMenu.counts.length > index ? String(catalogMenu.counts[index]) : ""
                checkable: true
                checked: picker.choice === index
                keepsMenuOpen: false
                onTriggered: picker.chooseCatalog(index)
            }
            onObjectAdded: (index, object) => catalogMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => catalogMenu.removeItem(object)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "fontPickerCatalogManage"
            iconRole: "styles"
            text: qsTr("Manage catalogs…")
            onTriggered: if (FontWindows.catalogWindow) FontWindows.catalogWindow.openFor(picker.family, picker)
        }
    }
    // +: the family joins or leaves a catalog (AddToCatalog's menu), a new
    // catalog, or the fonts of the subtitles.
    ShellMenu {
        id: addMenu
        objectName: "fontPickerAddMenu"
        property var names: []
        function openAt(anchor) {
            names = picker.catalogs.catalogNames
            popup(anchor, 0, anchor.height)
        }
        ShellMenuItem {
            objectName: "fontPickerNoCatalogs"
            text: qsTr("No catalogs yet")
            enabled: false
            visible: addMenu.names.length === 0
            height: visible ? implicitHeight : 0
        }
        Instantiator {
            model: addMenu.names
            delegate: ShellMenuItem {
                required property string modelData
                objectName: "fontPickerAddItem"
                text: modelData
                checkable: true
                checked: picker.catalogs.isFontInCatalog(modelData, picker.family)
                keepsMenuOpen: false // ITEM_CHECK_AND_HIDE (FontCatalogList.cpp:687)
                onTriggered: picker.catalogs.toggleFontInCatalog(modelData, picker.family, checked)
            }
            onObjectAdded: (index, object) => addMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => addMenu.removeItem(object)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "fontPickerNewCatalog"
            iconRole: "add"
            text: qsTr("New catalog…")
            onTriggered: if (FontWindows.catalogWindow) FontWindows.catalogWindow.startNew(picker.family, picker)
        }
        ShellMenuItem {
            objectName: "fontsFromSubtitlesItem"
            iconRole: "fonts-from-subtitles"
            text: qsTr("Add fonts from subtitles…")
            onTriggered: if (FontWindows.fromSubtitles)
                             FontWindows.fromSubtitles.openFor(picker.catalogChosen ? picker.choices[picker.choice] : "",
                                                               name => {
                                                                   picker.catalogsEdited()
                                                                   const at = picker.choices.indexOf(name)
                                                                   if (at >= 0)
                                                                       picker.chooseCatalog(at)
                                                               }, picker)
        }
    }
    // The characters filter (STYLE_EDIT_FILTER_TEXT and _ON): Apply keeps
    // the text and turns it on; Clear turns it off.
    Popover {
        id: charactersPopover
        objectName: "fontPickerCharactersPopover"
        title: qsTr("Only fonts that have these characters")
        showTitle: true
        initialFocus: charactersField
        function openFor(anchorItem) {
            charactersField.text = picker.catalogs.filterText
            openAt(anchorItem)
        }
        function apply() {
            keep()
            picker.catalogs.applyFilterText(charactersField.text)
            picker.catalogs.setFilterOn(charactersField.text.length > 0)
            picker.relist()
        }
        TextField {
            id: charactersField
            objectName: "fontPickerCharactersText"
            Accessible.name: qsTr("Characters")
            Layout.preferredWidth: 260
            onAccepted: charactersPopover.apply()
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "fontPickerCharactersClear"
                text: qsTr("Clear")
                onClicked: {
                    charactersPopover.keep()
                    picker.catalogs.setFilterOn(false)
                    picker.relist()
                }
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                objectName: "fontPickerCharactersApply"
                text: qsTr("Apply")
                onClicked: charactersPopover.apply()
            }
        }
    }
}
