pragma ComponentBehavior: Bound
// Y6: legacy FontCatalogList "Manage font catalogs" (FontCatalogList.cpp:
// 94-360 at 20d647c4), a modeless resizable window. Edits are autosaved to
// FontCatalogsAutosave0..2.txt 20 s after the first, and FontCatalogs.txt
// is written when the window is shown or hidden (CATALOG_CHANGED) and, once
// a window was made, when the application ends (~FontCatalogList).
//
// PU3 (#305): one window on DG1's frame for the program (FontWindows), never
// made inside the font picker or the Style manager that open it:
// * the catalogs on the leading side with their sizes, under one icon
//   strip: New and Rename (popovers at the button, their name errors
//   inline), Delete (a message box), Load from a file (the clash review for
//   the catalogs that exist) and Add fonts from subtitles;
// * every installed font with a check saying whether the chosen catalog
//   holds it (Space toggles the selected rows), its catalogs and a sample
//   (PU3-catalog-membership: legacy's mark column and per-row catalog
//   menu); Show narrows the list; "Refresh fonts" (F47-refresh) reads the
//   fonts again;
// * the selected font's preview by the subtitle renderer;
// * "Saved" in the footer while the safety copy is fresh, the file in its
//   tooltip (legacy's status bar and its sentence), and Close.
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: window
    objectName: "fontCatalogWindow"
    required property var catalogs // FontCatalogsController
    property var notices: null // ToastStack
    title: qsTr("Manage font catalogs")
    dialogId: "fontCatalogs"
    resizable: true
    property bool created: false
    property var fonts: []        // GetFonts when the window was made
    property string chosen: ""    // the catalog on the leading side
    property int revision: 0      // the catalogs changed
    property int show: 0          // 0 all fonts, 1 in the chosen catalog, 2 in none
    property var selected: ({})   // the selected rows' fonts
    property int resolveRequest: -1
    property var resolution: ({})
    property string resolvedFor: "" // the characters the last lookup asked for
    property bool extending: false // a Ctrl click moves the current row, keeping the selection
    property string selectedFont: "" // the current row's font, kept when the list changes under it
    // Show "In …" lists the chosen catalog: another catalog, another list.
    onChosenChanged: if (created && show === 1) selectFont(selectedFont)
    // CATALOG_CHANGED, sent when the window is shown and when it is hidden.
    signal catalogChanged()

    // The fonts the chosen catalog holds, by name.
    readonly property var members: {
        revision
        const set = Object.create(null)
        for (const f of fonts)
            if (chosen.length > 0 && catalogs.isFontInCatalog(chosen, f))
                set[f] = true
        return set
    }
    readonly property var shown: {
        revision
        const term = search.text.trim().toLowerCase()
        return fonts.filter(f => (term.length === 0 || f.toLowerCase().indexOf(term) >= 0)
                                 && (show !== 1 || members[f] === true)
                                 && (show !== 2 || catalogs.catalogOf(f).length === 0))
    }
    readonly property string currentFont: list.currentIndex >= 0 && list.currentIndex < shown.length ? shown[list.currentIndex] : ""

    // GenerateList(styleFont): every font, the font given selected (the
    // first equal ignoring case, else the first), and the first catalog
    // holding it chosen.
    function create(font) {
        fonts = catalogs.allFonts()
        created = true
        catalogs.windowMade() // ~FontCatalogList saves at the end
        const holding = catalogs.catalogsOf(font || "")
        chosen = holding.length > 0 ? holding[0] : catalogs.catalogNames.length > 0 ? catalogs.catalogNames[0] : ""
        show = 0
        search.text = ""
        selectFont(font)
    }
    // Opened from the picker or the Style editor (`opener`, their window)
    // with their font: the whole list, that font selected. Shown for
    // another window already, it closes first, so that it serves (and is
    // not blocked by) the new opener.
    function openFor(font, opener) {
        // (Not for a window opened from this one: it would close with it.)
        if (visible && opener && transientParent !== opener && opener.transientParent !== window)
            close()
        if (!created) {
            create(font)
        } else {
            search.text = ""
            show = 0
            selectFont(font)
        }
        openFrom(opener || null)
    }
    // + › New catalog… in the picker: the window, then New's popover.
    function startNew(font, opener) {
        openFor(font, opener)
        Qt.callLater(() => newPopover.openFor(newButton))
    }
    function selectFont(font) {
        const row = font ? catalogs.styleFontIndex(shown, font) : 0
        select(row)
    }
    function select(row) {
        list.currentIndex = row
        list.positionViewAtIndex(Math.max(0, row - 2), ListView.Beginning) // ScrollTo(sel - 2)
        const f = row >= 0 && row < shown.length ? shown[row] : ""
        const set = Object.create(null)
        if (f.length > 0)
            set[f] = true
        selected = set
        selectedFont = f
        refreshPreview()
    }
    // SV1: drawn again on a screen of another scale, or when the setting
    // changes (Window.devicePixelRatio, Qt 6.11).
    readonly property real previewPixelRatio: preview.Window.window ? preview.Window.window.devicePixelRatio : 1
    onPreviewPixelRatioChanged: refreshPreview()
    Connections {
        target: window.catalogs
        function onPreviewAtScreenSizeChanged() { window.refreshPreview() }
    }
    function refreshPreview() {
        if (currentFont.length === 0) {
            // No row (nothing matches): no preview and no resolution.
            resolveRequest = -1
            resolution = ({})
            return
        }
        catalogs.renderPreview(currentFont, Math.max(1, preview.width), Math.max(1, preview.height),
                               previewPixelRatio)
        // The preview Style is bold: the bold face is the one drawn, with
        // the preview text's characters. Until this request's answer no
        // other face is said.
        resolution = ({})
        resolvedFor = catalogs.samplePlain(catalogs.previewText, true)
        resolveRequest = catalogs.resolveFamily(currentFont, true, false, false, resolvedFor)
    }
    // The selected fonts join or leave the chosen catalog (CatalogList's
    // menu over the marked rows).
    function setMembership(names, add) {
        if (chosen.length === 0 || names.length === 0)
            return
        catalogs.setFontsInCatalog(chosen, names, add)
    }
    function toggleSelected() {
        const names = shown.filter(f => selected[f] === true)
        if (names.length === 0)
            return
        // The current row decides while it is marked (a Ctrl-click can
        // unmark it), else the first marked one.
        const lead = selected[currentFont] === true ? currentFont : names[0]
        setMembership(names, !(members[lead] === true))
    }
    function clickRow(row, modifiers) {
        const f = shown[row]
        if (modifiers & Qt.ControlModifier) {
            const set = Object.assign(Object.create(null), selected)
            if (set[f])
                delete set[f]
            else
                set[f] = true
            selected = set
            selectedFont = f
            extending = true
            list.currentIndex = row
            extending = false
        } else if ((modifiers & Qt.ShiftModifier) && list.currentIndex >= 0) {
            const set = Object.create(null)
            for (let i = Math.min(row, list.currentIndex); i <= Math.max(row, list.currentIndex); ++i)
                set[shown[i]] = true
            selected = set
        } else {
            select(row)
            return
        }
        refreshPreview()
    }

    // --- Catalogs -------------------------------------------------------
    // The name typed is taken when a catalog has it ignoring case (other
    // than the one renamed).
    function nameProblem(name, renaming) {
        const n = name.trim()
        if (n.length === 0)
            return renaming ? qsTr("Enter a name for the catalog.") : ""
        const existing = catalogs.existingName(n)
        if (existing.length === 0 || existing === renaming)
            return ""
        return renaming ? "" : qsTr("“%1” already exists.").arg(existing)
    }
    function stepCatalog(by) {
        const names = catalogs.catalogNames
        if (names.length === 0)
            return
        const at = names.indexOf(chosen)
        chosen = names[at < 0 ? 0 : Math.max(0, Math.min(names.length - 1, at + by))]
    }
    function createCatalog(name) {
        catalogs.addCatalog(name, true)
        chosen = name
    }
    // ChangeCatalogName: onto another catalog's name, the question first.
    function renameCatalog(oldName, newName) {
        const existing = catalogs.existingName(newName)
        if (existing.length > 0 && existing !== oldName) {
            ask({
                title: qsTr("Rename font catalog"),
                text: qsTr("A catalog named “%1” already exists.").arg(existing),
                detail: qsTr("Merge adds the fonts of “%1” to it. Replace deletes the old “%2” first.").arg(oldName).arg(existing),
                severity: "warning",
                choices: [{ id: "replace", text: qsTr("Replace"), role: "destructive" },
                          { id: "merge", text: qsTr("Merge"), role: "accept", isDefault: true },
                          { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
            }, id => {
                if (id === "merge" || id === "replace")
                    renamed(oldName, existing, id === "merge" ? 0 : 1)
            })
            return
        }
        renamed(oldName, newName, 0)
    }
    function renamed(oldName, newName, answer) {
        if (catalogs.renameCatalog(oldName, newName, answer))
            chosen = newName
    }
    function askDelete() {
        const name = chosen
        if (name.length === 0)
            return
        const size = catalogs.catalogSize(name)
        ask({
            title: qsTr("Delete font catalog"),
            text: qsTr("Delete the catalog “%1”?").arg(name),
            detail: size === 0 ? qsTr("It holds no fonts.") : qsTr("Its fonts stay installed and in their other catalogs."),
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id !== "delete")
                return
            const names = catalogs.catalogNames
            const at = names.indexOf(name)
            catalogs.removeCatalog(name)
            const left = catalogs.catalogNames
            chosen = left.length > 0 ? left[Math.min(at, left.length - 1)] : ""
        })
    }
    // Load (PU3-load-review): the file's catalogs; those that exist with
    // other fonts go to the clash review, Merge (legacy's) first.
    function load(file) {
        const r = catalogs.reviewCatalogFile(file)
        if (!r.ok) {
            if (notices)
                notices.post("warning", qsTr("Load font catalogs"), qsTr("The file holds no font catalog."), { details: r.file })
            return
        }
        if (r.rows.length === 0) {
            loaded(catalogs.loadReviewed([]), r.file)
            return
        }
        // Names a rename cannot take: yours and the file's, ignoring case.
        const taken = catalogs.catalogNames.concat(r.names)
        const suggest = name => {
            for (let i = 2; ; ++i) {
                const n = qsTr("%1 (%2)").arg(name).arg(i)
                if (catalogs.sameNameIndex(taken, n) < 0)
                    return n
            }
        }
        reviewLoader.active = true
        reviewLoader.item.review({
            title: qsTr("Load font catalogs"),
            text: qsTr("%1 of %2 catalogs in “%3” already exist.").arg(r.rows.length).arg(r.total).arg(r.file),
            detail: [r.total - r.rows.length - r.same > 0 ? qsTr("The file’s new catalogs are added.") : "",
                     r.same > 0 ? qsTr("Those with the same fonts as yours are left as they are.") : ""]
                    .filter(t => t.length > 0).join(" "),
            canMerge: true,
            action: "merge",
            nameHeading: qsTr("Catalog"),
            differenceHeading: qsTr("Fonts"),
            rows: r.rows.map(row => ({ name: row.name,
                                       // The fonts here, and those the file would add (or its count when it adds none).
                                       difference: row.added > 0 ? qsTr("%1 here, +%2 new").arg(row.here).arg(row.added)
                                                                 : qsTr("%1 here, %2 in the file").arg(row.here).arg(row.there),
                                       suggestion: suggest(row.name) })),
            added: r.total - r.rows.length - r.same,
            namePattern: /.*/, // a catalog name may hold a comma
            nameTaken: (name, row) => {
                const review = reviewLoader.item
                const renames = review.names.filter((n, i) => i !== row && review.actions[i] === "rename").map(n => n.trim())
                return catalogs.sameNameIndex(taken, name) >= 0 || catalogs.sameNameIndex(renames, name) >= 0
            },
            acceptText: count => count === 1 ? qsTr("Load 1 catalog") : qsTr("Load %n catalogs", "", count)
        }, results => {
            if (results)
                loaded(catalogs.loadReviewed(results.map(x => ({ name: x.name, action: x.action, newName: x.newName }))), r.file)
        })
    }
    function loaded(result, file) {
        if (notices && result.loaded > 0) {
            const parts = []
            if (result.added > 0) parts.push(qsTr("%n new", "", result.added))
            if (result.merged > 0) parts.push(qsTr("%n merged", "", result.merged))
            if (result.replaced > 0) parts.push(qsTr("%n replaced", "", result.replaced))
            if (result.renamed > 0) parts.push(qsTr("%n renamed", "", result.renamed))
            notices.post("info", qsTr("Load font catalogs"),
                         result.loaded === 1 ? qsTr("Loaded 1 font catalog") : qsTr("Loaded %n font catalogs", "", result.loaded),
                         { details: qsTr("From %1: %2.").arg(file).arg(parts.join(", ")) })
        }
    }
    // PK1's message box: made at the first question.
    function ask(options, done) {
        boxLoader.active = true
        boxLoader.item.ask(options, done)
    }

    onVisibleChanged: {
        if (!created)
            return
        catalogs.save()
        catalogChanged()
    }
    Connections {
        target: window.catalogs
        function onCatalogsChanged() {
            window.revision++
            // A catalog gone, or the first loaded into an empty list: the first.
            if (window.created && !window.catalogs.catalogExists(window.chosen)) {
                window.chosen = window.catalogs.catalogNames.length > 0 ? window.catalogs.catalogNames[0] : ""
                if (window.chosen.length === 0 && window.show === 1)
                    window.show = 0 // no catalog to show the fonts of: every font
            }
            // Show "In …" or "Not in a catalog": a membership change takes rows
            // out or in; the current font stays if it is still listed.
            if (window.created && window.show !== 0)
                window.selectFont(window.selectedFont)
        }
        // FontEnum's observers: a font change lists the fonts again.
        function onFontsChanged() {
            if (!window.created)
                return
            const font = window.currentFont
            window.fonts = window.catalogs.allFonts()
            window.selectFont(font)
        }
        // The preview text changed (the Style editor's, shared): the image
        // and the lookup follow its characters.
        function onFilterChanged() {
            if (window.created && window.visible && window.currentFont.length > 0
                    && window.catalogs.samplePlain(window.catalogs.previewText, true) !== window.resolvedFor)
                window.refreshPreview()
        }
        function onResolutionReady(requestId, result) {
            if (requestId === window.resolveRequest)
                window.resolution = result
        }
    }

    RowLayout {
        spacing: 16
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 6
        // The catalogs.
        ColumnLayout {
            spacing: 6
            Layout.preferredWidth: 200
            Layout.maximumWidth: 200
            Layout.fillHeight: true
            Label { text: qsTr("Catalogs"); color: Theme.muted; font: window.fontAt(11) }
            RowLayout {
                spacing: 2
                IconToolButton {
                    id: newButton
                    objectName: "fontCatalogNew"
                    iconRole: "add"
                    text: qsTr("New catalog")
                    onClicked: newPopover.openFor(newButton)
                }
                IconToolButton {
                    id: renameButton
                    objectName: "fontCatalogRename"
                    iconRole: "edit"
                    text: qsTr("Rename")
                    enabled: window.chosen.length > 0
                    onClicked: renamePopover.openFor(renameButton)
                }
                IconToolButton {
                    objectName: "fontCatalogDelete"
                    iconRole: "delete"
                    text: qsTr("Delete")
                    enabled: window.chosen.length > 0
                    onClicked: window.askDelete()
                }
                Rectangle { width: 1; height: 18; color: Theme.line; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                IconToolButton {
                    objectName: "fontCatalogLoad"
                    iconRole: "import"
                    text: qsTr("Load from a file")
                    onClicked: loadDialog.open()
                }
                IconToolButton {
                    objectName: "fontCatalogFromSubtitles"
                    iconRole: "fonts-from-subtitles"
                    text: qsTr("Add fonts from subtitles")
                    onClicked: if (FontWindows.fromSubtitles)
                                   FontWindows.fromSubtitles.openFor(window.chosen, name => window.chosen = name, window)
                }
            }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                Label {
                    textFormat: Text.PlainText
                    visible: catalogList.count === 0
                    anchors.fill: parent
                    anchors.margins: 10
                    text: qsTr("No catalogs yet")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
                ListView {
                    id: catalogList
                    objectName: "fontCatalogCatalogs"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    // Read once made: the catalogs load at their first use.
                    model: window.created ? window.catalogs.catalogNames : []
                    currentIndex: model.indexOf(window.chosen)
                    highlightMoveDuration: 0
                    // Up and Down choose the catalog (the current row follows `chosen`).
                    Keys.onUpPressed: window.stepCatalog(-1)
                    Keys.onDownPressed: window.stepCatalog(1)
                    Accessible.name: qsTr("Catalogs")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: catalogRow
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        height: 28
                        highlighted: ListView.isCurrentItem
                        Accessible.name: modelData
                        onClicked: window.chosen = modelData
                        background: Rectangle {
                            color: catalogRow.highlighted ? Theme.select : catalogRow.hovered ? Theme.raised : "transparent"
                            Rectangle { visible: catalogRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                        }
                        contentItem: RowLayout {
                            Label {
                                textFormat: Text.PlainText
                                text: catalogRow.modelData
                                font.weight: catalogRow.highlighted ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: window.revision >= 0 ? window.catalogs.catalogSize(catalogRow.modelData) : ""
                                color: Theme.muted
                                font: window.fontAt(12)
                            }
                        }
                    }
                }
            }
        }
        // The fonts.
        ColumnLayout {
            spacing: 8
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                spacing: 6
                TextField {
                    id: search
                    objectName: "fontCatalogFind"
                    placeholderText: qsTr("Search fonts")
                    Accessible.name: qsTr("Search fonts")
                    Layout.fillWidth: true
                    leftPadding: 28
                    onTextEdited: window.select(0)
                    Keys.onDownPressed: list.forceActiveFocus()
                    Icon {
                        iconRole: "search"
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                IconToolButton {
                    objectName: "fontCatalogRefreshFonts"
                    iconRole: "refresh"
                    text: qsTr("Refresh fonts")
                    tip: qsTr("Refresh fonts: reads the installed and external fonts again")
                    onClicked: {
                        const font = window.currentFont
                        window.catalogs.refreshFonts()
                        window.fonts = window.catalogs.allFonts()
                        window.selectFont(font)
                    }
                }
            }
            // Show, on its own row: its choices keep their words.
            RowLayout {
                spacing: 8
                Label { text: qsTr("Show"); color: Theme.muted }
                SegmentedButtons {
                    id: showChoice
                    objectName: "fontCatalogShow"
                    currentIndex: window.show
                    choices: [{ text: qsTr("All fonts"), name: "fontCatalogShowAll" },
                              { text: window.chosen.length > 0 ? qsTr("In “%1”").arg(window.chosen) : qsTr("In the catalog"),
                                name: "fontCatalogShowIn", enabled: window.chosen.length > 0 },
                              { text: qsTr("Not in a catalog"), name: "fontCatalogShowNone" }]
                    Accessible.name: qsTr("Show")
                    // The cells share the widest choice's width (in bold, as when chosen).
                    readonly property real widest: Math.max(...choices.map(c => showMetrics.advanceWidth(c.text)))
                    Layout.preferredWidth: choices.length * (widest + 24) + 2
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.fillWidth: true
                    onActivated: index => { window.show = index; window.select(0) }
                    FontMetrics { id: showMetrics; font.bold: true }
                }
            }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                Layout.preferredWidth: 600
                Layout.preferredHeight: 340
                Rectangle {
                    id: header
                    x: 1; y: 1
                    width: parent.width - 2
                    height: 26
                    color: Theme.raised
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        spacing: 10
                        Item { implicitWidth: 20 }
                        Label { text: qsTr("Font"); color: Theme.muted; font: window.fontAt(12); Layout.preferredWidth: 170 }
                        Label { text: qsTr("Catalogs"); color: Theme.muted; font: window.fontAt(12); Layout.preferredWidth: 130 }
                        Label {
                            textFormat: Text.PlainText
                            text: qsTr("Sample")
                            color: Theme.muted
                            font: window.fontAt(12)
                            Layout.fillWidth: true
                            // fonts.md: a Qt-drawn sample is labelled as one.
                            ToolTip.visible: sampleHover.hovered
                            ToolTip.text: qsTr("Drawn by Qt, not by the subtitle renderer")
                            HoverHandler { id: sampleHover }
                        }
                    }
                }
                Label {
                    textFormat: Text.PlainText
                    visible: window.shown.length === 0
                    x: 12; y: header.height + 10
                    text: qsTr("No fonts match")
                    color: Theme.muted
                }
                ListView {
                    id: list
                    objectName: "fontCatalogList"
                    x: 1
                    y: header.height + 1
                    width: parent.width - 2
                    height: parent.height - header.height - 2
                    clip: true
                    model: window.shown
                    currentIndex: -1
                    highlightMoveDuration: 0
                    keyNavigationEnabled: true
                    Accessible.name: qsTr("Fonts")
                    ScrollBar.vertical: ScrollBar {}
                    onCurrentIndexChanged: if (activeFocus && !window.extending) window.select(currentIndex)
                    Keys.onSpacePressed: window.toggleSelected()
                    delegate: ItemDelegate {
                        id: row
                        required property string modelData
                        required property int index
                        readonly property bool isSelected: window.selected[modelData] === true
                        objectName: "fontCatalogRow"
                        width: ListView.view.width
                        height: 28
                        highlighted: isSelected
                        Accessible.name: modelData
                        // The current row has the list's focus: Space toggles
                        // the selection, not a press of the row.
                        Keys.onSpacePressed: window.toggleSelected()
                        background: Rectangle {
                            color: row.isSelected ? Theme.select : row.hovered ? Theme.raised : "transparent"
                            Rectangle { visible: row.isSelected && row.ListView.isCurrentItem; width: 3; height: parent.height; color: Theme.accent }
                            // A click selects; Ctrl adds or removes, Shift a range.
                            MouseArea {
                                anchors.fill: parent
                                onClicked: mouse => {
                                    list.forceActiveFocus()
                                    window.clickRow(row.index, mouse.modifiers)
                                }
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            CheckBox {
                                objectName: "fontCatalogMember"
                                padding: 0
                                enabled: window.chosen.length > 0
                                checked: window.members[row.modelData] === true
                                Accessible.name: qsTr("“%1” in “%2”").arg(row.modelData).arg(window.chosen)
                                onToggled: window.setMembership([row.modelData], checked)
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: row.modelData
                                elide: Text.ElideRight
                                Layout.preferredWidth: 170
                            }
                            Label {
                                textFormat: Text.PlainText
                                readonly property var held: window.revision >= 0 ? window.catalogs.catalogsOf(row.modelData) : []
                                text: held.length > 0 ? held.join(", ") : "—"
                                color: Theme.muted
                                font: window.fontAt(12)
                                elide: Text.ElideRight
                                Layout.preferredWidth: 130
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: window.catalogs.previewText
                                font.family: row.modelData
                                font.pixelSize: 15
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Accessible.ignored: true
                            }
                        }
                    }
                }
            }
            // The selected font's preview, by the subtitle renderer.
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                Image {
                    id: preview
                    objectName: "fontCatalogPreview"
                    anchors.fill: parent
                    anchors.margins: 1
                    cache: false
                    // SV1: drawn with the screen's pixels, shown at the
                    // preview's size (Image ignores the image's pixel ratio).
                    fillMode: window.catalogs.previewScale > 1 ? Image.PreserveAspectFit : Image.Pad
                    visible: window.currentFont.length > 0
                    source: window.catalogs.previewKey > 0 ? "image://fontcatalogpreview/" + window.catalogs.previewKey : ""
                    onWidthChanged: window.refreshPreview()
                    onHeightChanged: window.refreshPreview()
                }
            }
            FontResolutionNote {
                objectName: "fontCatalogResolution"
                resolution: window.resolution
                Layout.fillWidth: true
            }
        }
    }

    leadingActions: [
        Label {
            textFormat: Text.PlainText
            objectName: "fontCatalogStatus"
            visible: window.catalogs.autosaveFailed || window.catalogs.autosaveStatus.length > 0
            text: window.catalogs.autosaveFailed ? qsTr("Not saved: the safety copy could not be written") : qsTr("Saved")
            color: window.catalogs.autosaveFailed ? Theme.warning : Theme.muted
            ToolTip.visible: savedHover.hovered
            ToolTip.text: qsTr("The catalogs are saved automatically to FontCatalogs.txt in the \"Config\" folder.")
            HoverHandler { id: savedHover }
        }
    ]
    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "fontCatalogClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: window.reject()
        }
    ]
    defaultButton: closeButton

    // New and Rename: the name in a popover at the button.
    Popover {
        id: newPopover
        objectName: "fontCatalogNewPopover"
        title: qsTr("New catalog")
        showTitle: true
        initialFocus: newName
        readonly property string problem: window.nameProblem(newName.text, "")
        function openFor(anchorItem) {
            newName.text = ""
            openAt(anchorItem)
        }
        function create() {
            const name = newName.text.trim()
            if (name.length === 0 || problem !== "")
                return
            keep()
            window.createCatalog(name)
        }
        TextField {
            id: newName
            objectName: "fontCatalogNewName"
            placeholderText: qsTr("Catalog name")
            Accessible.name: qsTr("Catalog name")
            Layout.preferredWidth: 240
            onAccepted: newPopover.create()
        }
        Label {
            textFormat: Text.PlainText
            objectName: "fontCatalogNewProblem"
            visible: newPopover.problem !== ""
            text: newPopover.problem
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.preferredWidth: 240
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            PrimaryButton {
                objectName: "fontCatalogNewCreate"
                text: qsTr("Create")
                enabled: newName.text.trim().length > 0 && newPopover.problem === ""
                onClicked: newPopover.create()
            }
            Button { text: qsTr("Cancel"); onClicked: newPopover.revert() }
        }
    }
    Popover {
        id: renamePopover
        objectName: "fontCatalogRenamePopover"
        title: qsTr("Rename “%1”").arg(renamePopover.oldName)
        showTitle: true
        initialFocus: renameField
        property string oldName: ""
        readonly property string problem: window.nameProblem(renameField.text, oldName)
        function openFor(anchorItem) {
            oldName = window.chosen
            renameField.text = oldName
            renameField.selectAll()
            openAt(anchorItem)
        }
        function rename() {
            const name = renameField.text.trim()
            if (problem !== "")
                return
            keep()
            if (name !== oldName)
                window.renameCatalog(oldName, name)
        }
        TextField {
            id: renameField
            objectName: "fontCatalogRenameName"
            Accessible.name: qsTr("Catalog name")
            Layout.preferredWidth: 240
            onAccepted: renamePopover.rename()
        }
        Label {
            textFormat: Text.PlainText
            objectName: "fontCatalogRenameProblem"
            visible: renamePopover.problem !== ""
            text: renamePopover.problem
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.preferredWidth: 240
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            PrimaryButton {
                objectName: "fontCatalogRenameOk"
                text: qsTr("Rename")
                enabled: renamePopover.problem === ""
                onClicked: renamePopover.rename()
            }
            Button { text: qsTr("Cancel"); onClicked: renamePopover.revert() }
        }
    }

    Loader {
        id: boxLoader
        visible: false // a window: no room in the layout
        active: false
        sourceComponent: MessageBox {
            objectName: "fontCatalogMessageBox"
            settings: window.settings
        }
    }
    // PK1: made at the first clash.
    Loader {
        id: reviewLoader
        visible: false
        active: false
        sourceComponent: ClashReview {
            objectName: "fontCatalogClashReview"
            dialogId: "fontCatalogClashes"
            settings: window.settings
        }
    }
    // OnLoadCatalogs (FontCatalogList.cpp:315-316): "Text files (*.txt)",
    // whose pattern Qt reads from the label. Titled for catalog files
    // (Y6-load-title: legacy's read "Choose video file").
    Dialogs.FileDialog {
        id: loadDialog
        objectName: "fontCatalogLoadDialog"
        title: qsTr("Choose font catalog file")
        nameFilters: [qsTr("Text files (*.txt)")]
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: window.load(selectedFile)
    }
}
