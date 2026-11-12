pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: window
    objectName: "fontCatalogWindow"
    required property var catalogs
    property var notices: null
    title: qsTr("Manage font catalogues")
    dialogId: "fontCatalogs"
    resizable: true
    property bool created: false
    property var fonts: []
    property string chosen: ""
    property int revision: 0
    // MUST KILL: show is a magic int (0 all fonts, 1 in the chosen catalog, 2 in none)
    property int show: 0
    property var selected: ({})
    property int resolveRequest: -1
    property var resolution: ({})
    property string resolvedFor: ""
    property bool extending: false
    property string selectedFont: ""
    property bool chosenHidden: false
    readonly property string chosenTier: chosenHidden ? "mine" : ""
    readonly property var catalogRows: { revision; return created ? catalogs.catalogRows : [] }
    readonly property int chosenRow: catalogRows.findIndex(r => r.name === chosen && r.hidden === chosenHidden)
    function choose(name, hidden) {
        chosenHidden = hidden === true
        chosen = name
    }
    onChosenChanged: if (created && show === 1) selectFont(selectedFont)
    signal catalogChanged()

    readonly property var members: {
        revision
        const set = Object.create(null)
        for (const f of fonts)
            if (chosen.length > 0 && catalogs.isFontInCatalog(chosen, f, chosenTier))
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

    function create(font) {
        fonts = catalogs.allFonts()
        created = true
        catalogs.windowMade()
        const holding = catalogs.catalogsOf(font || "")
        choose(holding.length > 0 ? holding[0] : catalogs.catalogNames.length > 0 ? catalogs.catalogNames[0] : "")
        show = 0
        search.text = ""
        selectFont(font)
    }
    function openFor(font, opener) {
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
        list.positionViewAtIndex(Math.max(0, row - 2), ListView.Beginning)
        const f = row >= 0 && row < shown.length ? shown[row] : ""
        const set = Object.create(null)
        if (f.length > 0)
            set[f] = true
        selected = set
        selectedFont = f
        refreshPreview()
    }
    readonly property real previewPixelRatio: preview.Window.window ? preview.Window.window.devicePixelRatio : 1
    onPreviewPixelRatioChanged: refreshPreview()
    Connections {
        target: window.catalogs
        function onPreviewAtScreenSizeChanged() { window.refreshPreview() }
    }
    function refreshPreview() {
        if (currentFont.length === 0) {
            resolveRequest = -1
            resolution = ({})
            return
        }
        catalogs.renderPreview(currentFont, Math.max(1, preview.width), Math.max(1, preview.height),
                               previewPixelRatio)
        resolution = ({})
        resolvedFor = catalogs.samplePlain(catalogs.previewText, true)
        resolveRequest = catalogs.resolveFamily(currentFont, true, false, false, resolvedFor)
    }
    function setMembership(names, add) {
        if (chosen.length === 0 || names.length === 0 || chosenHidden)
            return
        catalogs.setFontsInCatalog(chosen, names, add)
    }
    function toggleSelected() {
        const names = shown.filter(f => selected[f] === true)
        if (names.length === 0)
            return
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

    function nameProblem(name, renaming) {
        const n = name.trim()
        if (n.length === 0)
            return renaming ? qsTr("Enter a name for the catalogue.") : ""
        const existing = catalogs.existingName(n)
        if (existing.length === 0 || existing === renaming)
            return ""
        return renaming ? "" : qsTr("“%1” already exists.").arg(existing)
    }
    function stepCatalog(by) {
        const rows = catalogRows
        if (rows.length === 0)
            return
        const row = rows[chosenRow < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, chosenRow + by))]
        choose(row.name, row.hidden)
    }
    function createCatalog(name) {
        createCatalogIn(name, "")
    }
    function createCatalogIn(name, tier) {
        catalogs.addCatalog(name, true, tier)
        choose(name)
    }
    function transfer(move) {
        const row = catalogRows[chosenRow]
        if (!row)
            return
        const run = choices => {
            const landed = move ? catalogs.moveCatalog(row.name, row.tier, choices) : catalogs.copyCatalog(row.name, row.tier, choices)
            if (landed.length > 0)
                choose(landed)
        }
        const clashes = catalogs.catalogClashes(row.name, row.tier)
        if (clashes.length === 0) {
            run([])
            return
        }
        reviewLoader.active = true
        reviewLoader.item.review({
            title: move ? qsTr("Move font catalogue") : qsTr("Copy font catalogue"),
            text: row.tier === "mine" ? qsTr("The project already has a catalogue named “%1”.").arg(clashes[0].name)
                                      : qsTr("You already have a catalogue named “%1”.").arg(clashes[0].name),
            nameHeading: qsTr("Catalogue"),
            rows: clashes.map(c => ({ name: c.name, difference: "", suggestion: c.suggestion })),
            namePattern: /.*/,
            acceptText: count => move ? qsTr("Move") : qsTr("Copy"),
            nameTaken: n => catalogRows.some(r => r.tier !== row.tier && r.name.toLowerCase() === n.trim().toLowerCase())
        }, results => {
            if (results)
                run(results)
        })
    }
    function renameCatalog(oldName, newName) {
        const existing = catalogs.existingName(newName)
        if (existing.length > 0 && existing !== oldName) {
            ask({
                title: qsTr("Rename font catalogue"),
                text: qsTr("A catalogue named “%1” already exists.").arg(existing),
                detail: qsTr("Merge adds the fonts of “%1” to it. Replace deletes the old “%2” first.").arg(oldName, existing),
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
            choose(newName)
    }
    function askDelete() {
        const name = chosen
        if (name.length === 0)
            return
        const size = catalogs.catalogSize(name)
        ask({
            title: qsTr("Delete font catalogue"),
            text: qsTr("Delete the catalogue “%1”?").arg(name),
            detail: size === 0 ? qsTr("It holds no fonts.") : qsTr("Its fonts stay installed and in their other catalogues."),
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
            choose(left.length > 0 ? left[Math.min(at, left.length - 1)] : "")
        })
    }
    function load(file) {
        const r = catalogs.reviewCatalogFile(file)
        if (!r.ok) {
            if (notices)
                notices.post("warning", qsTr("Load font catalogues"), qsTr("The file holds no font catalogue."), { details: r.file })
            return
        }
        if (r.rows.length === 0) {
            loaded(catalogs.loadReviewed([]), r.file)
            return
        }
        const taken = catalogs.catalogNames.concat(r.names)
        const suggest = name => {
            for (let i = 2; ; ++i) {
                const n = qsTr("%1 (%2)").arg(name, i)
                if (catalogs.sameNameIndex(taken, n) < 0)
                    return n
            }
        }
        reviewLoader.active = true
        reviewLoader.item.review({
            title: qsTr("Load font catalogues"),
            text: qsTr("%1 of %2 catalogues in “%3” already exist.").arg(r.rows.length, r.total, r.file),
            detail: [r.total - r.rows.length - r.same > 0 ? qsTr("The file’s new catalogues are added.") : "",
                     r.same > 0 ? qsTr("Those with the same fonts as yours are left as they are.") : ""]
                    .filter(t => t.length > 0).join(" "),
            canMerge: true,
            action: "merge",
            nameHeading: qsTr("Catalogue"),
            differenceHeading: qsTr("Fonts"),
            rows: r.rows.map(row => ({ name: row.name,
                                       difference: row.added > 0 ? qsTr("%1 here, +%2 new").arg(row.here, row.added)
                                                                 : qsTr("%1 here, %2 in the file").arg(row.here, row.there),
                                       suggestion: suggest(row.name) })),
            added: r.total - r.rows.length - r.same,
            namePattern: /.*/,
            nameTaken: (name, row) => {
                const review = reviewLoader.item
                const renames = review.names.filter((n, i) => i !== row && review.actions[i] === "rename").map(n => n.trim())
                return catalogs.sameNameIndex(taken, name) >= 0 || catalogs.sameNameIndex(renames, name) >= 0
            },
            acceptText: count => count === 1 ? qsTr("Load 1 catalogue") : qsTr("Load %n catalogues", "", count)
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
            notices.post("info", qsTr("Load font catalogues"),
                         result.loaded === 1 ? qsTr("Loaded 1 font catalogue") : qsTr("Loaded %n font catalogues", "", result.loaded),
                         { details: qsTr("From %1: %2.").arg(file, parts.join(", ")) })
        }
    }
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
            if (window.created && window.chosenRow < 0) {
                window.choose(window.catalogs.catalogNames.length > 0 ? window.catalogs.catalogNames[0] : "")
                if (window.chosen.length === 0 && window.show === 1)
                    window.show = 0
            }
            if (window.created && window.show !== 0)
                window.selectFont(window.selectedFont)
        }
        function onFontsChanged() {
            if (!window.created)
                return
            const font = window.currentFont
            window.fonts = window.catalogs.allFonts()
            window.selectFont(font)
        }
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
        ColumnLayout {
            spacing: 6
            Layout.preferredWidth: 200
            Layout.maximumWidth: 200
            Layout.fillHeight: true
            Label { text: qsTr("Catalogues"); color: Theme.muted; font: window.fontAt(11) }
            RowLayout {
                spacing: 2
                IconToolButton {
                    id: newButton
                    objectName: "fontCatalogNew"
                    iconRole: "add"
                    text: qsTr("New catalogue")
                    onClicked: newPopover.openFor(newButton)
                }
                IconToolButton {
                    id: renameButton
                    objectName: "fontCatalogRename"
                    iconRole: "edit"
                    text: qsTr("Rename")
                    enabled: window.chosen.length > 0 && !window.chosenHidden
                    onClicked: renamePopover.openFor(renameButton)
                }
                IconToolButton {
                    objectName: "fontCatalogDelete"
                    iconRole: "delete"
                    text: qsTr("Delete")
                    enabled: window.chosen.length > 0 && !window.chosenHidden
                    onClicked: window.askDelete()
                }
                IconToolButton {
                    id: tierButton
                    objectName: "fontCatalogTiers"
                    visible: window.catalogs.hasProject
                    iconRole: "panel-menu"
                    text: qsTr("Catalogue tiers")
                    enabled: window.chosen.length > 0
                    onClicked: tierMenu.popup(tierButton, 0, tierButton.height)
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
                    enabled: !window.chosenHidden
                    onClicked: if (FontWindows.fromSubtitles)
                                   FontWindows.fromSubtitles.openFor(window.chosen, name => window.choose(name), window)
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
                    text: qsTr("No catalogues yet")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
                ListView {
                    id: catalogList
                    objectName: "fontCatalogCatalogs"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: window.catalogRows
                    currentIndex: window.chosenRow
                    highlightMoveDuration: 0
                    Keys.onUpPressed: window.stepCatalog(-1)
                    Keys.onDownPressed: window.stepCatalog(1)
                    Accessible.name: qsTr("Catalogues")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Column {
                        id: catalogSlot
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        Label {
                            textFormat: Text.PlainText
                            visible: window.catalogs.hasProject
                                     && (catalogSlot.index === 0 || window.catalogRows[catalogSlot.index - 1].tier !== catalogSlot.modelData.tier)
                            text: catalogSlot.modelData.tier === "project" ? qsTr("This project") : qsTr("Mine")
                            color: Theme.muted
                            font: window.fontAt(11)
                            leftPadding: 8
                            topPadding: 6
                            bottomPadding: 2
                        }
                        ItemDelegate {
                            id: catalogRow
                            readonly property var modelData: catalogSlot.modelData
                            objectName: "fontCatalogCatalog" + catalogSlot.index
                            width: catalogSlot.width
                            height: 28
                            highlighted: catalogSlot.ListView.isCurrentItem
                            Accessible.name: modelData.hidden ? qsTr("%1, hidden by the project’s copy").arg(modelData.name) : modelData.name
                            onClicked: window.choose(modelData.name, modelData.hidden)
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                enabled: window.catalogs.hasProject
                                onTapped: {
                                    window.choose(catalogRow.modelData.name, catalogRow.modelData.hidden)
                                    tierMenu.popup()
                                }
                            }
                            background: Rectangle {
                                color: catalogRow.highlighted ? Theme.select : catalogRow.hovered ? Theme.raised : "transparent"
                                Rectangle { visible: catalogRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                Label {
                                    textFormat: Text.PlainText
                                    text: catalogRow.modelData.name
                                    color: catalogRow.modelData.hidden ? Theme.muted : Theme.text
                                    font.weight: catalogRow.highlighted ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                TierChip {
                                    visible: window.catalogs.hasProject
                                    tier: catalogRow.modelData.tier
                                    enabled: !catalogRow.modelData.hidden
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: window.revision >= 0
                                          ? window.catalogs.catalogSize(catalogRow.modelData.name, catalogRow.modelData.hidden ? "mine" : "") : ""
                                    color: Theme.muted
                                    font: window.fontAt(12)
                                }
                            }
                        }
                    }
                }
            }
            Label {
                textFormat: Text.PlainText
                objectName: "fontCatalogHidden"
                visible: window.chosenHidden
                text: qsTr("Hidden by the project’s copy. Copy it to the project to use it there.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
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
            RowLayout {
                spacing: 8
                Label { text: qsTr("Show"); color: Theme.muted }
                SegmentedButtons {
                    id: showChoice
                    objectName: "fontCatalogShow"
                    currentIndex: window.show
                    choices: [{ text: qsTr("All fonts"), name: "fontCatalogShowAll" },
                              { text: window.chosen.length > 0 ? qsTr("In “%1”").arg(window.chosen) : qsTr("In the catalogue"),
                                name: "fontCatalogShowIn", enabled: window.chosen.length > 0 },
                              { text: qsTr("Not in a catalogue"), name: "fontCatalogShowNone" }]
                    Accessible.name: qsTr("Show")
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
                        Label { text: qsTr("Catalogues"); color: Theme.muted; font: window.fontAt(12); Layout.preferredWidth: 130 }
                        Label {
                            textFormat: Text.PlainText
                            text: qsTr("Sample")
                            color: Theme.muted
                            font: window.fontAt(12)
                            Layout.fillWidth: true
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
                        Keys.onSpacePressed: window.toggleSelected()
                        background: Rectangle {
                            color: row.isSelected ? Theme.select : row.hovered ? Theme.raised : "transparent"
                            Rectangle { visible: row.isSelected && row.ListView.isCurrentItem; width: 3; height: parent.height; color: Theme.accent }
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
                                enabled: window.chosen.length > 0 && !window.chosenHidden
                                checked: window.members[row.modelData] === true
                                Accessible.name: qsTr("“%1” in “%2”").arg(row.modelData, window.chosen)
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
                                objectName: "fontCatalogSample"
                                textFormat: Text.PlainText
                                text: window.catalogs.samplePlain(window.catalogs.previewText, true)
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
                    // Image ignores the image's pixel ratio.
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
            ToolTip.text: qsTr("Catalogues are saved automatically to fonts.toml in the “catalogs” folder of your user folder.")
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

    Popover {
        id: newPopover
        objectName: "fontCatalogNewPopover"
        title: qsTr("New catalogue")
        showTitle: true
        initialFocus: newName
        readonly property string problem: window.nameProblem(newName.text, "")
        function openFor(anchorItem) {
            newName.text = ""
            openAt(anchorItem)
        }
        function create(tier) {
            const name = newName.text.trim()
            if (name.length === 0 || problem !== "")
                return
            keep()
            window.createCatalogIn(name, tier)
        }
        TextField {
            id: newName
            objectName: "fontCatalogNewName"
            placeholderText: qsTr("Catalogue name")
            Accessible.name: qsTr("Catalogue name")
            Layout.preferredWidth: 240
            onAccepted: newPopover.create(newCatalogSave.tier)
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
            TierSaveButton {
                id: newCatalogSave
                objectName: "fontCatalogNewSave"
                faceName: "fontCatalogNewCreate"
                listId: "fontCatalogs"
                settings: window.settings
                hasProject: window.catalogs.hasProject
                plainText: qsTr("Create")
                faceEnabled: newName.text.trim().length > 0 && newPopover.problem === ""
                onSave: tier => newPopover.create(tier)
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
            Accessible.name: qsTr("Catalogue name")
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

    ShellMenu {
        id: tierMenu
        objectName: "fontCatalogTierMenu"
        readonly property bool fromMine: (window.catalogRows[window.chosenRow]?.tier ?? "mine") === "mine"
        ShellMenuItem {
            objectName: "copyFontCatalogToTier"
            iconRole: "copy-to-tier"
            text: tierMenu.fromMine ? qsTr("Copy to project") : qsTr("Copy to mine")
            onTriggered: window.transfer(false)
        }
        ShellMenuItem {
            objectName: "moveFontCatalogToTier"
            iconRole: "move-to-tier"
            text: tierMenu.fromMine ? qsTr("Move to project") : qsTr("Move to mine")
            onTriggered: window.transfer(true)
        }
    }
    Loader {
        id: boxLoader
        visible: false
        active: false
        sourceComponent: MessageBox {
            objectName: "fontCatalogMessageBox"
            settings: window.settings
        }
    }
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
    // Qt reads the pattern from the filter's label.
    FileChooser {
        id: loadDialog
        objectName: "fontCatalogLoadDialog"
        title: qsTr("Choose font catalogue file")
        nameFilters: [qsTr("Text files (*.txt)")]
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: window.load(selectedFile)
    }
}
