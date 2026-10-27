// SY1 (#247): the Style manager and its Style editor in one window on DG1's
// frame (legacy StyleStore with StyleChange), resizable, modeless, its place
// and size kept. The sidebar has the filter, the catalog with its Styles
// above and this file's Styles below (legacy's order), the copies between
// them and a strip of icon tools over each list. The editor is always
// beside the lists, on the selected Styles, under its pinned preview. One
// footer: Revert at the leading edge, then OK, Cancel and Apply.
// List actions (copy, delete, move, sort) take effect at once, as before.
pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: manager
    objectName: "styleManager"
    // K1: the set's styles icon as the window's (legacy stylestore SetIcon).
    Component.onCompleted: IconTheme.setWindowIcon(manager, "styles")
    required property var styles   // StyleManagerController
    required property var catalogs // Y6: FontCatalogsController
    required property ColourPickerController colourPicker
    required property var app
    // PK1's toasts (Main.qml's root.notices), for Delete unused styles.
    property var notices: null
    title: qsTr("Style manager")
    dialogId: "styleManager"
    settings: app ? app.settings : null
    resizable: true
    flush: true
    footerRule: true
    defaultButton: okButton
    initialFocus: assList

    property var storeSelected: []
    property var assSelected: []
    property bool editingStore: false
    property bool editing: false
    // The values the edited Style had when the editor opened on it (what
    // Revert and each field's reset put back).
    property var values: ({})
    // The fields that differ between the selected Styles ("Multiple
    // values"), and those the editor opened with (Revert).
    property var mixed: []
    property var openedMixed: []
    // The "Multiple values" fields set by hand: written to every Style,
    // even at the first one's value.
    property var resolved: []
    // New or Duplicate: a Style not in its list until Apply or OK.
    property bool pendingNew: false
    // The fields edited since the editor opened ({key: true}).
    property var changedKeys: ({})
    readonly property bool dirty: editing && (pendingNew || Object.keys(changedKeys).length > 0)
    readonly property var editedRows: editingStore ? storeSelected : assSelected
    readonly property bool multi: editing && !pendingNew && editedRows.length > 1
    property var storeRows: []
    property var assRows: []
    // The catalog storeRows came from.
    property string listedCatalog: ""
    // The manager's own change of a list (a commit, a delete): it sets the
    // selections itself.
    property bool ownChange: false
    // Another tab's question about an edit not applied is open: the lists
    // and the edit stay on the file it was made in until it is answered.
    property bool awaitingSwitch: false
    readonly property string filterText: filterField.text.trim().toLowerCase()

    // --- Opening ----------------------------------------------------------
    function showFor(styleName) {
        const go = () => {
            refresh()
            const row = rowNamed(false, styleName)
            if (styles.available)
                choose(false, row >= 0 ? [row] : shownOf(false, [0]))
            else
                choose(true, shownOf(true, [0]))
        }
        if (shown())
            guard(go)
        else
            go()
        open()
    }
    // LE6 (#231): the Line editor's Edit style: the editor on the Line's
    // Style, its name field focused, whatever the window showed; a Style
    // the Document lacks is offered for creation, from its Default.
    function editStyle(styleName) {
        const go = () => {
            refresh()
            const row = rowNamed(false, styleName)
            if (row >= 0) {
                editDocumentStyle(row)
            } else if (styleName.length > 0 && styles.available) {
                askCreate(styleName)
            }
        }
        if (shown()) {
            open() // raised
            guard(go)
            return
        }
        // Closed: open on the file's Styles; a missing Style is asked about
        // over them.
        refresh()
        const row = rowNamed(false, styleName)
        if (row < 0)
            choose(false, shownOf(false, [0]))
        open()
        go()
    }
    // TP1 (#274): the Styles panel's rows are the Document's, a name
    // possibly twice. The window on `row` (-1: the first the filter shows);
    // an edit in progress keeps both lists' selections.
    function showAt(row) {
        if (!editing) {
            refresh()
            choose(false, row >= 0 && row < assRows.length ? [row] : shownOf(false, [0]))
        }
        open()
    }
    // The Styles panel's New, Edit, Duplicate and Copy to the catalog, as
    // the window's own buttons do them on the file's list (an edit not
    // applied is asked about first).
    function onDocumentRow(row, then) {
        if (!shown())
            refresh()
        open()
        guard(() => {
            manager.refresh()
            if (row >= 0 && row < manager.assRows.length) {
                manager.choose(false, [row])
                then(row)
            }
        })
    }
    function newDocumentStyle() {
        if (!shown())
            refresh()
        open()
        startNew(false, false)
    }
    function editStyleAt(row) { onDocumentRow(row, r => manager.editDocumentStyle(r)) }
    function duplicateDocumentStyle(row) { onDocumentRow(row, () => manager.startNew(false, true)) }
    function copyStyleToCatalog(row) { onDocumentRow(row, () => manager.copyToCatalog()) }
    function editDocumentStyle(row) {
        choose(false, [row])
        requestActivate() // also after a question's own window
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function askCreate(styleName) {
        const key = styles.listsKey()
        ask({
            title: qsTr("Edit style"),
            text: qsTr("This file has no style named “%1”.").arg(styleName),
            detail: qsTr("Create it from Default and edit it?"),
            choices: [{ id: "create", text: qsTr("Create"), role: "accept", isDefault: true },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
        }, id => { if (id === "create" && manager.styles.listsKey() === key) manager.createStyle(styleName) })
    }
    function createStyle(styleName) {
        const v = styles.beginNamed(styleName)
        if (v.name === undefined)
            return
        const problem = own(() => styles.commitEdit(v, [], false, false))
        if (problem !== "") {
            styles.endEdit()
            showError(qsTr("New style"), problem)
            return
        }
        editDocumentStyle(rowNamed(false, styleName))
    }
    function closeManager() { close() }
    // Open, or hidden only while its frame is made again (Wayland,
    // MainWindowFrame::apply), which is no closing.
    function shown() { return visible || WindowFrame.remaking(manager) }
    onVisibleChanged: {
        if (shown())
            return
        styles.saveCatalog() // OnClose saves the store
        endEditing()
    }

    // --- The lists ----------------------------------------------------------
    function refresh() {
        styles.refreshLists()
        reloadRows()
    }
    function reloadRows() {
        storeRows = styles.styleRows(true)
        assRows = styles.styleRows(false)
        listedCatalog = styles.catalog
    }
    // The manager's own change of a list (a commit, a move, a copy): its
    // rows are set by the action itself, not followed by name (a name may
    // repeat, and the list emits its change before the action returns).
    function own(action) {
        ownChange = true
        try {
            return action()
        } finally {
            ownChange = false
        }
    }
    // A list's rows after the manager changed it: selected, and the editor
    // on them where it was on that list (an edit not applied stays, pinned
    // to its Style's row now).
    function followRows(store, rows) {
        if (store)
            storeSelected = rows
        else
            assSelected = rows
        if (!editing || pendingNew || editingStore !== store)
            return
        if (rows.length === 0)
            endEditing()
        else if (dirty)
            rebase(store, rows[0], rows)
        else
            openOn(store, rows)
    }
    function sameRows(a, b) { return a.length === b.length && a.every((r, i) => r === b[i]) }
    // The lists again after a change made elsewhere (an undo, a move,
    // Settings' catalog): the selections follow their Styles by name (a
    // repeated name's by its copy) and the editor with them; an edit not
    // applied stays on its Style while it is there.
    function reloadFollowing() {
        if (awaitingSwitch)
            return
        if (ownChange || !shown()) {
            reloadRows()
            return
        }
        const storeKeys = keysOf(true, storeSelected)
        const assKeys = keysOf(false, assSelected)
        const opened = editing && !pendingNew ? keysOf(editingStore, editedRows.slice(0, 1)) : []
        const ownKey = opened.length > 0 && opened[0].name === values.name ? opened[0] : { name: values.name, copy: 0 }
        const catalogBefore = listedCatalog
        reloadRows()
        if (editing && editingStore && styles.editCatalog() !== styles.catalog) {
            catalogMoved()
            return
        }
        const storeFound = catalogBefore === listedCatalog ? sorted(rowsOfKeys(true, storeKeys)) : []
        const assFound = sorted(rowsOfKeys(false, assKeys))
        if (!editing || pendingNew) {
            storeSelected = storeFound
            assSelected = assFound
            return
        }
        const store = editingStore
        const found = store ? storeFound : assFound
        const own = rowsOfKeys(store, [ownKey])
        if (dirty && own.length > 0) {
            storeSelected = storeFound
            assSelected = assFound
            rebase(store, own[0], found)
            return
        }
        if (dirty)
            dropped(values.name, qsTr("The style is gone."))
        const other = store ? assFound : storeFound
        if (found.length > 0 && sameRows(found, editedRows)) {
            // Opened again only when its values moved (an edit, say, at
            // each Line typed would cost a preview).
            const map = styles.beginEdit(store, found[0])
            const fields = found.length > 1 ? styles.mixedFields(store, found) : []
            if (Object.keys(map).some(k => !same(map[k], values[k])) || !sameRows(fields.slice().sort(), mixed.slice().sort()))
                beginMixed(map, store, fields)
        } else {
            choose(store, found)
        }
        if (store)
            assSelected = other
        else
            storeSelected = other
    }
    // An edit not applied over its Style changed elsewhere (an undo): the
    // fields not edited take the Style's values now, the edited ones stay,
    // so Apply writes only what was edited over them.
    function rebase(store, row, rows) {
        const edits = editor.values()
        const kept = Object.keys(changedKeys)
        const map = styles.beginEdit(store, row) // pinned again, at its values now
        if (map.name === undefined)
            return
        const fields = rows.length > 1 ? styles.mixedFields(store, rows) : []
        if (!Object.keys(map).some(k => !same(map[k], values[k])) && sameRows(fields.slice().sort(), openedMixed.slice().sort()))
            return
        values = map
        openedMixed = fields
        mixed = fields.filter(k => kept.indexOf(k) < 0)
        // A field edited that the Styles now differ in is set by hand (a
        // "Multiple values" field): written to each.
        resolved = kept.filter(k => fields.indexOf(k) >= 0)
        const v = Object.assign({}, map)
        for (const k of kept)
            v[k] = edits[k]
        editor.load(v)
        refreshChanges()
        updatePreview()
    }
    // Settings loaded another catalog under an edit of a catalog Style: the
    // edit is asked about (Apply writes it to its own catalog), then the
    // catalog shown is.
    function catalogMoved() {
        if (!dirty) {
            catalogShown()
            return
        }
        if (messages.asking)
            return // answered first; the catalog is followed then
        ask({
            title: qsTr("Edit style"),
            text: qsTr("Apply the changes to “%1”?").arg(pendingNew ? nameField.text : values.name),
            detail: qsTr("The catalog “%1” is shown now; the changes go to “%2”.").arg(styles.catalog).arg(styles.editCatalog()),
            severity: "warning",
            choices: [{ id: "discard", text: qsTr("Discard"), role: "destructive" },
                      { id: "apply", text: qsTr("Apply"), role: "accept", isDefault: true }]
        }, id => {
            if (id === "apply")
                manager.applyAway()
            manager.revert()
            manager.catalogShown()
        })
    }
    function rowNamed(store, name) {
        const rows = store ? storeRows : assRows
        return rows.findIndex(r => r.name === name)
    }
    // Each of `rows` as its name and its copy of the name (0 for the first
    // "Dup", 1 for the second), to be found again after a change; in one
    // pass, as rowsOfKeys: a file may hold thousands of Styles.
    function keysOf(store, rows) {
        const list = store ? storeRows : assRows
        const wanted = Object.create(null)
        for (const r of rows)
            wanted[r] = true
        const seen = Object.create(null)
        const at = Object.create(null)
        for (let i = 0; i < list.length; ++i) {
            const copy = seen[list[i].name] || 0
            seen[list[i].name] = copy + 1
            if (wanted[i])
                at[i] = { name: list[i].name, copy: copy }
        }
        return rows.filter(r => at[r] !== undefined).map(r => at[r])
    }
    // The rows of `keys` (keysOf) now, each once: a name's copy at its
    // place, or its last copy when the name has fewer now.
    function rowsOfKeys(store, keys) {
        const list = store ? storeRows : assRows
        const at = Object.create(null)
        for (let i = 0; i < list.length; ++i)
            (at[list[i].name] = at[list[i].name] || []).push(i)
        const taken = Object.create(null)
        const out = []
        for (const key of keys) {
            const copies = at[key.name]
            if (!copies)
                continue
            const r = copies[Math.min(key.copy, copies.length - 1)]
            if (!taken[r]) {
                taken[r] = true
                out.push(r)
            }
        }
        return out
    }
    // `rows` the filter shows; if none, the shown row nearest the first.
    function shownOf(store, rows) {
        const shown = (store ? storeShown : assShown).filter(s => !s.pending).map(s => s.row)
        if (shown.length === 0 || rows.length === 0)
            return []
        const visible = Object.create(null)
        for (const r of shown)
            visible[r] = true
        const kept = rows.filter(r => visible[r])
        if (kept.length > 0)
            return kept
        const after = shown.find(r => r >= rows[0])
        return [after !== undefined ? after : shown[shown.length - 1]]
    }
    function sorted(list) { return list.slice().sort((a, b) => a - b) }
    // Selects `rows` in one list (the other's selection goes) and opens the
    // editor on them.
    function choose(store, rows) {
        rows = sorted(rows)
        storeSelected = store ? rows : []
        assSelected = store ? [] : rows
        openOn(store, rows)
        const list = store ? storeList : assList
        const at = rows.length > 0 ? list.indexOf(rows[0]) : -1
        if (at >= 0) {
            list.currentIndex = at
            list.positionViewAtIndex(at, ListView.Contain)
        }
    }
    function openOn(store, rows) {
        pendingNew = false
        if (rows.length === 0) {
            endEditing()
            return
        }
        beginMixed(styles.beginEdit(store, rows[0]), store, rows.length > 1 ? styles.mixedFields(store, rows) : [])
    }
    // A pick in a list, a New, a tab change: an edit not applied is asked
    // about first (Apply, Discard or Cancel), then `next` runs.
    function guard(next) {
        if (!dirty) {
            next()
            return
        }
        const key = styles.listsKey()
        ask({
            title: qsTr("Edit style"),
            text: multi ? qsTr("Apply the changes to the %n selected styles?", "", editedRows.length)
                        : qsTr("Apply the changes to “%1”?").arg(pendingNew ? nameField.text : values.name),
            severity: "warning",
            choices: [{ id: "discard", text: qsTr("Discard"), role: "destructive" },
                      { id: "apply", text: qsTr("Apply"), role: "accept", isDefault: true },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
        }, id => {
            // The lists changed under the question (the main window showed
            // another tab, say): a Cancel; the switch asks its own.
            if (manager.styles.listsKey() !== key)
                return
            if (id === "apply") {
                if (manager.commit(false))
                    next()
            } else if (id === "discard") {
                manager.revert() // the selection stays open, as it was
                next()
            }
        })
    }
    function pick(store, rows) {
        guard(() => manager.choose(store, rows))
    }
    function startNew(store, copy) {
        const from = store ? storeSelected : assSelected
        guard(() => {
            const map = copy ? styles.beginCopy(store, from[0]) : styles.beginNew(store)
            if (map.name === undefined)
                return
            storeSelected = []
            assSelected = []
            beginEditing(map, store)
            pendingNew = true
            nameField.forceActiveFocus()
            nameField.selectAll()
        })
    }
    function loadFrom(store, anchor) {
        guard(() => {
            loadDialog.toStore = store
            loadDialog.anchor = anchor
            loadDialog.key = manager.styles.listsKey()
            loadDialog.open()
        })
    }
    function deleteSelected(store) {
        const rows = store ? storeSelected : assSelected
        if (rows.length === 0)
            return
        const left = shownOf(store, own(() => styles.removeStyles(store, rows)))
        if (editing && editingStore === store)
            choose(store, left)
        else if (store)
            storeSelected = left
        else
            assSelected = left
    }
    function moveSelected(store, kind) {
        const rows = store ? storeSelected : assSelected
        if (rows.length === 0)
            return
        followRows(store, own(() => styles.moveStyles(store, rows, kind)))
    }
    // The selection follows its Styles by name and copy (the sort is
    // stable: a repeated name's Styles keep their order).
    function sortList(store) {
        const keys = keysOf(store, store ? storeSelected : assSelected)
        own(() => styles.sortStyles(store))
        followRows(store, sorted(rowsOfKeys(store, keys)))
    }
    // SY1-clean-toast: Delete unused styles (the file's ⋯ menu and the
    // Subtitles menu's command): a toast with the Styles deleted and the
    // legacy report in the Log, where StyleStore showed a message.
    function deleteUnused() {
        const done = () => {
            const keys = keysOf(false, assSelected) // a name's copies go or stay together
            const result = own(() => styles.deleteUnusedStyles())
            if (result.deleted === undefined)
                return
            const deleted = result.deleted
            if (notices)
                notices.post("info", deleted.length === 0 ? qsTr("No unused styles")
                                   : deleted.length === 1 ? qsTr("Deleted 1 unused style")
                                   : qsTr("Deleted %n unused styles", "", deleted.length),
                             deleted.length === 0 ? qsTr("Every style is used by a line.")
                                                  : deleted.join(", ") + "\n" + qsTr("Ctrl+Z puts them back."),
                             { details: result.report })
            const found = rowsOfKeys(false, keys)
            if (shown() && editing && !editingStore)
                choose(false, shownOf(false, found.length > 0 ? found : [0]))
            else
                assSelected = sorted(found)
        }
        if (shown() && editing && !editingStore)
            guard(done)
        else
            done()
    }

    // --- Copies -------------------------------------------------------------
    // PK1's clash review (it replaces legacy's chained "Style named \"%s\"
    // already exists. Replace?" Yes to all / Yes / No / Cancel). The clashes
    // that are the same both sides are left out and counted; Cancel copies
    // nothing.
    function fieldNames(fields) {
        const groups = [
            [1, qsTr("font")], [2, qsTr("size")], [4 | 8 | 16 | 32, qsTr("bold, italic, underline or strikeout")],
            [64 | 128 | 256 | 512, qsTr("colours")], [1024 | 65536, qsTr("border")], [2048, qsTr("shadow")],
            [4096 | 8192, qsTr("scale")], [16384, qsTr("angle")], [32768, qsTr("spacing")],
            [131072, qsTr("alignment")], [262144 | 524288 | 1048576, qsTr("margins")], [2097152, qsTr("encoding")]]
        const names = groups.filter(g => (fields & g[0]) !== 0).map(g => g[1])
        const text = names.join(", ")
        return text.length > 0 ? text.charAt(0).toLocaleUpperCase() + text.slice(1) : ""
    }
    // `clashes` from the controller (transferClashes, fileClashes); `total`
    // the Styles copied; `into` names where they go. done(choices), only
    // while the lists are as they were at `key` (listsKey: by default now).
    function reviewClashes(clashes, total, into, done, key) {
        const since = key !== undefined ? key : styles.listsKey()
        const listed = clashes.filter(c => !c.same)
        const same = clashes.length - listed.length
        if (listed.length === 0) {
            done([])
            return
        }
        clashReviewLoader.active = true
        clashReviewLoader.item.review({
            title: qsTr("Copy styles"),
            text: qsTr("%1 of %2 styles already exist in %3.").arg(listed.length).arg(total).arg(into),
            detail: same > 0 ? qsTr("%n more are the same as the ones there and are left as they are.", "", same) : "",
            rows: listed.map(c => ({ name: c.name, occurrence: c.occurrence, difference: manager.fieldNames(c.fields), suggestion: c.suggestion })),
            added: total - clashes.length,
            acceptText: count => qsTr("Copy %n style(s)", "", count)
        }, results => {
            if (!results)
                return
            if (manager.styles.listsKey() !== since) {
                // The rows and the clashes were of the lists then.
                manager.showError(qsTr("Copy styles"), qsTr("The styles changed while this was open. Nothing was copied."))
                return
            }
            done(results)
        })
    }
    // Add to file, Add to catalog, Add to all open files: the selection's
    // Styles; the copies are selected in the list they went to.
    function copyToFile(all) {
        guard(() => {
            const rows = storeSelected
            reviewClashes(styles.transferClashes(false, rows), rows.length, qsTr("this file"), choices => {
                if (all)
                    manager.own(() => styles.addToAllDocuments(rows, choices))
                else
                    manager.choose(false, manager.own(() => styles.addToDocument(rows, choices)))
            })
        })
    }
    function copyToCatalog() {
        guard(() => {
            const rows = assSelected
            reviewClashes(styles.transferClashes(true, rows), rows.length, qsTr("the catalog"), choices => {
                manager.choose(true, manager.own(() => styles.addToStore(rows, choices)))
            })
        })
    }
    // PK1: made at the first clash (a window made at the start costs it).
    Loader {
        id: clashReviewLoader
        active: false
        sourceComponent: ClashReview {
            objectName: "styleClashReview"
            dialogId: "styleClashes"
            settings: manager.app.settings
        }
    }
    // NT1's message host (PK1's box, made at the first): the questions and
    // the errors, one after another.
    function ask(options, done) { messages.ask(options, done) }
    function showError(title, text) {
        ask({ title: title, text: text, severity: "error" })
    }
    MessageHost {
        id: messages
        objectName: "styleMessages"
        settings: manager.app.settings
    }

    // --- Catalogs -----------------------------------------------------------
    function chooseCatalog(name) {
        guard(() => {
            if (manager.own(() => styles.chooseCatalog(name)))
                manager.catalogShown()
        })
    }
    // Another catalog's Styles: its first selected when the editor was on
    // the catalog.
    function catalogShown() {
        if (editing && editingStore)
            choose(true, shownOf(true, [0]))
        else
            storeSelected = []
    }
    function createCatalog(name) {
        guard(() => {
            if (manager.own(() => styles.createCatalog(name)))
                manager.catalogShown()
        })
    }
    function askDeleteCatalog() {
        const name = styles.catalog
        ask({
            title: qsTr("Delete catalog"),
            text: qsTr("Delete the catalog “%1”?").arg(name),
            detail: storeRows.length === 0 ? qsTr("It has no styles.")
                  : storeRows.length === 1 ? qsTr("Its one style goes with it. This can’t be undone.")
                  : qsTr("Its %n styles go with it. This can’t be undone.", "", storeRows.length),
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id !== "delete")
                return
            if (manager.editing && manager.editingStore)
                manager.endEditing()
            if (manager.own(() => manager.styles.deleteCatalog(name)))
                manager.catalogShown()
        })
    }

    // --- The editor ---------------------------------------------------------
    function beginEditing(map, store) { beginMixed(map, store, []) }
    // `mixedKeys`: the fields that differ in a multiple selection.
    function beginMixed(map, store, mixedKeys) {
        if (map.name === undefined)
            return
        pendingNew = false
        editingStore = store
        values = map
        mixed = mixedKeys
        openedMixed = mixed
        resolved = []
        renameBox.checked = true
        // Y6: StyleChange makes its font list once (ChangeCatalog in its constructor).
        if (!fontCatalogBar.made) {
            fontCatalogBar.made = true
            fontCatalogBar.reset()
        }
        editing = true // (before the fields: a multiple selection leaves the name empty)
        editor.load(map)
        refreshChanges()
        updatePreview()
    }
    function endEditing() {
        editing = false
        pendingNew = false
        mixed = []
        changedKeys = ({})
        styles.endEdit()
    }
    function isMixed(key) { return mixed.indexOf(key) >= 0 }
    // A field edited by hand: no longer "Multiple values".
    function edited(key) {
        if (isMixed(key)) {
            mixed = mixed.filter(k => k !== key)
            resolved = resolved.concat([key])
        }
        refreshChanges()
        updatePreview()
    }
    function same(a, b) {
        if (a !== null && b !== null && typeof a === "object" && typeof b === "object")
            return a.r === b.r && a.g === b.g && a.b === b.b && a.a === b.a
        return String(a) === String(b)
    }
    function refreshChanges() {
        if (!editing) {
            changedKeys = ({})
            return
        }
        const v = editor.values()
        const out = {}
        for (const key in v)
            if (!same(v[key], values[key]))
                out[key] = true
        for (const key of resolved) // set by hand, maybe to the first Style's value
            out[key] = true
        changedKeys = out
    }
    // A field set by hand back to "Multiple values".
    function unresolve(key) {
        if (resolved.indexOf(key) < 0)
            return
        resolved = resolved.filter(k => k !== key)
        if (!isMixed(key))
            mixed = mixed.concat([key])
    }
    // A label's reset: its fields' values when the editor opened.
    function resetFields(keys) {
        const v = editor.values()
        for (const key of keys) {
            v[key] = values[key]
            unresolve(key)
        }
        editor.load(v)
        refreshChanges()
        updatePreview()
    }
    function revert() {
        if (pendingNew) {
            endEditing()
            return
        }
        mixed = openedMixed
        resolved = []
        editor.load(values)
        refreshChanges()
        updatePreview()
    }
    // SY3: the sample line follows every field at once; while the font list
    // is open it shows the family under the keyboard or the pointer.
    // SV1: drawn again on a screen of another scale, or when the setting
    // changes (Window.devicePixelRatio, Qt 6.11).
    readonly property real previewPixelRatio: preview.Window.window ? preview.Window.window.devicePixelRatio : 1
    onPreviewPixelRatioChanged: updatePreview()
    Connections {
        target: manager.styles
        function onPreviewAtScreenSizeChanged() { manager.updatePreview() }
    }
    function updatePreview() {
        if (!editing)
            return
        const v = editor.values()
        const browsed = fontField.browsedFont()
        if (browsed.length)
            v.fontname = browsed
        styles.renderPreview(v, Math.max(1, preview.width), Math.max(1, preview.height), previewText.text,
                             previewPixelRatio)
    }
    readonly property string nameProblem: {
        if (!editing || multi)
            return ""
        const name = nameField.text
        if (name.trim().length === 0)
            return qsTr("A style needs a name.")
        // Its own name kept is no clash, a repeated one too (commitStyle).
        if (!pendingNew && name === values.name)
            return ""
        const rows = editingStore ? storeRows : assRows
        const own = pendingNew ? -1 : editedRows.length > 0 ? editedRows[0] : -1
        for (let i = 0; i < rows.length; ++i)
            if (i !== own && rows[i].name === name)
                return editingStore ? qsTr("The catalog already has a style named “%1”.").arg(name)
                                    : qsTr("This file already has a style named “%1”.").arg(name)
        return ""
    }
    readonly property int editedLines: !editingStore && editedRows.length === 1 && editedRows[0] < assRows.length
                                       ? assRows[editedRows[0]].lines : 0
    // ChangeStyle. A multiple selection writes the fields changed to each
    // (SY1-multi-edit); a renamed file Style is renamed in its Lines as the
    // check box says (SY1-rename-inline). Apply keeps the editor on the
    // Styles; OK closes the window. False when refused.
    function commit(close) {
        if (!editing) {
            if (close)
                accept()
            return true
        }
        if (nameProblem !== "")
            return false
        if (!dirty) {
            if (close)
                accept()
            return true
        }
        const store = editingStore
        const rows = editedRows
        const v = editor.values()
        const problem = commitValues(v)
        if (problem !== "") {
            showError(qsTr("Save style"), problem)
            return false
        }
        reloadRows()
        if (close) {
            accept()
            return true
        }
        choose(store, pendingNew ? [rowNamed(store, v.name)] : rows)
        return true
    }
    // The editor's values to the controller: "" or the refusal.
    function commitValues(v) {
        const many = multi
        return own(() => styles.commitEdit(v, many ? editedRows : [], many, renameBox.visible && renameBox.checked,
                                           many ? resolved : []))
    }
    // Apply as the lists move on (another tab, another catalog): a name
    // refused is put back and the other changes go in; what can't be
    // applied is said in a toast.
    function applyAway() {
        const name = pendingNew ? nameField.text : values.name
        if (!styles.editOpen()) {
            dropped(name, qsTr("Its file was closed."))
            return
        }
        let problem = !multi && nameField.text.trim().length === 0 ? qsTr("A style needs a name.")
                                                                    : commitValues(editor.values())
        if (problem !== "" && !pendingNew && !multi && nameField.text !== values.name) {
            const refused = nameField.text
            nameField.text = values.name
            if (commitValues(editor.values()) === "") {
                if (notices)
                    notices.post("warning", qsTr("Style not renamed"),
                                 qsTr("“%1” keeps its name; the other changes were applied. %2").arg(values.name).arg(problem),
                                 { details: refused })
                return
            }
        }
        if (problem !== "")
            dropped(name, problem)
    }
    // An edit not applied, and why.
    function dropped(name, why) {
        if (notices)
            notices.post("warning", qsTr("Changes to “%1” not applied").arg(name), why)
    }
    function fromFilter() {
        const list = assList.enabled && assList.count > 0 ? assList : storeList
        if (list.count === 0)
            return
        list.forceActiveFocus(Qt.TabFocusReason)
        list.pickIndex(0, Qt.NoModifier)
    }
    function focusEditor() {
        if (nameField.enabled)
            nameField.forceActiveFocus()
        else
            fontField.forceActiveFocus()
    }

    // The lists follow the controller, and the editing target: an edit, an
    // undo, or another tab (whose Styles the file list shows then; an edit
    // not applied is asked about, and applied to the file it was made in).
    Connections {
        target: manager.styles
        function onChanged() { manager.reloadFollowing() }
        function onTargetEdited(switched) {
            if (!manager.shown() || manager.awaitingSwitch)
                return // (a question open: its answer shows the tab then)
            if (!switched) {
                manager.reloadFollowing()
                return
            }
            // The new tab may name another catalog (Last Style Storage).
            const catalogMoves = manager.styles.followedCatalog() !== manager.styles.catalog
            const onStore = manager.editing && manager.editingStore
            const onFile = manager.editing && !manager.editingStore
            const shown = () => {
                manager.refresh()
                if (onFile)
                    manager.choose(false, manager.shownOf(false, [0])) // the first the filter shows
                else if (onStore && catalogMoves)
                    manager.choose(true, manager.shownOf(true, [0]))
                else
                    manager.assSelected = [] // a catalog Style stays open
            }
            // An edit not applied that the switch would leave on another
            // file or catalog is asked about first; the lists stay where the
            // edit was made until answered.
            if (!manager.dirty || (onStore && !catalogMoves)) {
                shown()
                return
            }
            // The file it was made in is closed: nothing to ask.
            if (onFile && !manager.styles.editOpen()) {
                manager.dropped(manager.pendingNew ? nameField.text : manager.values.name, qsTr("Its file was closed."))
                manager.revert()
                shown()
                return
            }
            manager.awaitingSwitch = true
            manager.ask({
                title: qsTr("Edit style"),
                text: qsTr("Apply the changes to “%1”?").arg(manager.pendingNew ? nameField.text : manager.values.name),
                detail: onStore ? qsTr("The file shown now uses the catalog “%1”.").arg(manager.styles.followedCatalog())
                                : qsTr("You edited it in the file shown before."),
                severity: "warning",
                choices: [{ id: "discard", text: qsTr("Discard"), role: "destructive" },
                          { id: "apply", text: qsTr("Apply"), role: "accept", isDefault: true }]
            }, id => {
                // Only Apply writes it: Discard, or the question closed,
                // drops the edit.
                if (id === "apply")
                    manager.applyAway()
                manager.awaitingSwitch = false
                manager.revert()
                shown()
            })
        }
    }

    // --- Keys ---------------------------------------------------------------
    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: { filterField.forceActiveFocus(); filterField.selectAll() }
    }
    // F6: the catalog list, this file's list, the editor, in turn; Shift+F6
    // the other way.
    function cycleRegions(step) {
        const regions = [storeList, assList, null].filter(r => r === null || r.enabled)
        let at = regions.findIndex(r => r !== null && r.activeFocus)
        if (at < 0)
            at = regions.length - 1 // the editor, or anywhere else
        const next = regions[(at + step + regions.length) % regions.length]
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
        else
            focusEditor()
    }
    Shortcut {
        sequence: "F6"
        onActivated: manager.cycleRegions(1)
    }
    Shortcut {
        sequence: "Shift+F6"
        onActivated: manager.cycleRegions(-1)
    }

    // --- Parts ----------------------------------------------------------------
    // A section of the editor: its title in bold, the fields under it.
    component SectionTitle: Label {
        font: manager.fontAt(13, Font.DemiBold)
        Layout.columnSpan: 4
        Layout.topMargin: 12
    }
    // A field's label, with a reset button while the field (or one of its
    // `keys`) differs from the value it opened with.
    component FieldLabel: RowLayout {
        id: fieldLabel
        property string text
        property string key
        property var keys: key.length > 0 ? [key] : []
        property string resetName: key
        readonly property var changed: keys.filter(k => manager.changedKeys[k] === true)
        spacing: 2
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.preferredHeight: 16
        Label {
            text: fieldLabel.text
            color: Theme.muted
            font: manager.fontAt(12)
            Layout.fillWidth: true
            Layout.minimumWidth: implicitWidth
        }
        IconToolButton {
            objectName: "styleReset_" + fieldLabel.resetName
            visible: fieldLabel.changed.length > 0 && !manager.pendingNew
            iconRole: "undo"
            iconSize: 14
            padding: 0
            implicitHeight: 16
            implicitWidth: 16
            text: qsTr("Reset %1").arg(fieldLabel.text)
            tip: qsTr("Put back the value it had")
            onClicked: manager.resetFields(fieldLabel.changed)
        }
    }
    // A field of the editor, "Multiple values" while the Styles differ, a
    // unit (%, °) at its end.
    component StyleField: TextField {
        id: field
        property string key
        property string unit: ""
        Layout.fillWidth: true
        Layout.preferredWidth: 60
        Layout.minimumWidth: 48
        rightPadding: unit.length > 0 ? 22 : leftPadding
        placeholderText: manager.isMixed(key) ? qsTr("Multiple values") : ""
        font.italic: manager.isMixed(key)
        onTextEdited: manager.edited(key)
        Label {
            visible: field.unit.length > 0
            text: field.unit
            color: Theme.muted
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    // Bold, Italic, Underline or Strikethrough: a toggle, with a dash under
    // its icon while the Styles differ.
    component EmphasisToggle: IconToolButton {
        id: toggle
        property string key
        readonly property bool isMixed: manager.isMixed(key)
        checkable: true
        tip: isMixed ? qsTr("%1: multiple values").arg(text) : text
        Accessible.description: isMixed ? qsTr("Multiple values") : ""
        onToggled: manager.edited(key)
        Rectangle {
            objectName: "styleMixedMark"
            visible: toggle.isMixed
            width: 8
            height: 2
            radius: 1
            color: Theme.muted
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
        }
    }
    // A Style's colour: its swatch (the hex in a chip; hatched while the
    // Styles differ) and its transparency (0-255).
    component ColourField: RowLayout {
        id: colourField
        required property int number
        required property string name
        required property string key
        property var colour: ({ r: 0, g: 0, b: 0, a: 0 })
        property Item swatch: swatchButton
        readonly property bool isMixed: manager.isMixed(key)
        spacing: 4
        Layout.fillWidth: true
        Layout.preferredWidth: 60
        function setColour(c) { colour = c }
        function showAlpha() {
            alphaField.value = colour.a
            alphaField.text = isMixed ? "" : String(colour.a)
        }
        onColourChanged: showAlpha()
        onIsMixedChanged: showAlpha()
        function hex(c) {
            return "#" + [c.r, c.g, c.b].map(v => Number(v).toString(16).padStart(2, "0")).join("").toUpperCase()
        }
        Button {
            id: swatchButton
            objectName: "styleColour" + colourField.number
            Accessible.name: colourField.name
            Accessible.description: colourField.isMixed ? qsTr("Multiple values") : colourField.hex(colourField.colour)
            Layout.fillWidth: true
            implicitHeight: 26
            ToolTip.visible: hovered
            ToolTip.text: colourField.name
            background: Rectangle {
                color: Qt.rgba(colourField.colour.r / 255, colourField.colour.g / 255, colourField.colour.b / 255, 1)
                border.color: swatchButton.visualFocus ? Theme.focus : Theme.control
                border.width: swatchButton.visualFocus ? 2 : 1
                radius: 2
                Canvas {
                    id: hatch
                    visible: colourField.isMixed
                    anchors.fill: parent
                    anchors.margins: 1
                    property color ink: Theme.muted
                    property color paper: Theme.field
                    onInkChanged: requestPaint()
                    onPaperChanged: requestPaint()
                    onVisibleChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        const g = getContext("2d")
                        g.fillStyle = paper
                        g.fillRect(0, 0, width, height)
                        g.strokeStyle = ink
                        g.lineWidth = 1
                        g.beginPath()
                        for (let x = -height; x < width; x += 6) {
                            g.moveTo(x, height)
                            g.lineTo(x + height, 0)
                        }
                        g.stroke()
                    }
                }
            }
            contentItem: Item {
                Rectangle {
                    visible: !colourField.isMixed
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.field
                    border.color: Theme.line
                    radius: 2
                    implicitWidth: hexLabel.implicitWidth + 6
                    implicitHeight: hexLabel.implicitHeight + 2
                    Label {
                        id: hexLabel
                        anchors.centerIn: parent
                        text: colourField.hex(colourField.colour)
                        font.family: "monospace"
                        font.pixelSize: 10
                    }
                }
            }
            onClicked: manager.colourClick(colourField.number, true)
            // Y7: the right click (wxEVT_RIGHT_UP, StyleChange.cpp:144-151).
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: manager.colourClick(colourField.number, false)
            }
        }
        TextField {
            id: alphaField
            objectName: "styleAlpha" + colourField.number
            // The transparency as a number (the old spin box's value).
            property int value: 0
            signal valueModified()
            Layout.preferredWidth: 46
            placeholderText: colourField.isMixed ? "–" : ""
            horizontalAlignment: TextInput.AlignRight
            validator: IntValidator { bottom: 0; top: 255 }
            Accessible.name: qsTr("%1 transparency").arg(colourField.name)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Transparency, 0 (opaque) to 255")
            onTextEdited: if (acceptableInput) { value = Number(text); valueModified() }
            Keys.onUpPressed: { value = Math.min(255, value + 1); valueModified() }
            Keys.onDownPressed: { value = Math.max(0, value - 1); valueModified() }
            onValueModified: {
                const c = colourField.colour
                colourField.colour = { r: c.r, g: c.g, b: c.b, a: value }
                manager.edited(colourField.key)
            }
        }
    }
    // A list of Styles: each row's swatch (the primary colour on the border
    // colour), its name, the unapplied mark on the edited one and, in the
    // file's list, the number of Lines using it. Click, Shift+click and
    // Ctrl+click select; the keys: Up, Down (Shift extends), Ctrl+A, Enter
    // to the name field, Delete, Alt+Up / Alt+Down / Alt+Home / Alt+End move.
    component StyleList: ListView {
        id: list
        required property bool store
        readonly property var selected: store ? manager.storeSelected : manager.assSelected
        property int anchorRow: -1
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        model: ListModel {}
        Accessible.role: Accessible.List
        Accessible.name: store ? qsTr("Catalog styles") : qsTr("Styles in this file")
        ScrollBar.vertical: ScrollBar {}
        // The model follows the rows in place, so the view keeps its place
        // while the file is edited.
        function sync(rows) {
            const m = model
            let i = 0
            for (; i < rows.length; ++i) {
                const r = rows[i]
                if (i < m.count) {
                    const o = m.get(i)
                    for (const k of ["row", "name", "primary", "outline", "lines", "pending"])
                        if (o[k] !== r[k])
                            m.setProperty(i, k, r[k])
                } else {
                    m.append(r)
                }
            }
            if (m.count > rows.length)
                m.remove(rows.length, m.count - rows.length)
            if (currentIndex >= m.count)
                currentIndex = m.count - 1
        }
        function rowAt(index) { return index >= 0 && index < model.count ? model.get(index).row : -1 }
        function indexOf(row) {
            for (let i = 0; i < model.count; ++i)
                if (model.get(i).row === row)
                    return i
            return -1
        }
        function pickIndex(index, modifiers) {
            const row = rowAt(index)
            if (row < 0)
                return
            currentIndex = index
            let rows = selected.slice()
            if (modifiers & Qt.ControlModifier) {
                const at = rows.indexOf(row)
                if (at >= 0)
                    rows.splice(at, 1)
                else
                    rows.push(row)
                anchorRow = row
            } else if ((modifiers & Qt.ShiftModifier) && anchorRow >= 0 && indexOf(anchorRow) >= 0) {
                const from = indexOf(anchorRow)
                rows = []
                for (let i = Math.min(from, index); i <= Math.max(from, index); ++i)
                    if (rowAt(i) >= 0)
                        rows.push(rowAt(i))
            } else {
                rows = [row]
                anchorRow = row
            }
            manager.pick(store, rows)
        }
        Keys.onPressed: event => {
            const alt = event.modifiers & Qt.AltModifier
            if (alt && [Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key)) {
                manager.moveSelected(store, { [Qt.Key_Home]: 0, [Qt.Key_Up]: 1, [Qt.Key_Down]: 2, [Qt.Key_End]: 3 }[event.key])
                Qt.callLater(() => { const i = list.indexOf(list.selected[0]); if (i >= 0) list.currentIndex = i })
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                const next = Math.max(0, Math.min(model.count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                pickIndex(next, event.modifiers & Qt.ShiftModifier)
            } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                const rows = []
                for (let i = 0; i < model.count; ++i)
                    if (rowAt(i) >= 0)
                        rows.push(rowAt(i))
                manager.pick(store, rows)
            } else if (event.key === Qt.Key_Delete) {
                manager.deleteSelected(store)
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (!manager.editing || manager.editingStore !== store || selected.length === 0)
                    return // the window's default button
                manager.focusEditor()
            } else {
                return
            }
            event.accepted = true
        }
        delegate: Rectangle {
            id: rowItem
            required property int index
            required property int row
            required property string name
            required property string primary
            required property string outline
            required property int lines
            required property bool pending
            readonly property bool chosen: pending || list.selected.indexOf(row) >= 0
            width: list.width
            height: 24
            color: chosen ? Theme.select : rowMouse.containsMouse ? Theme.raised : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: lines > 0 ? qsTr("%1, %n line(s)", "", lines).arg(name) : name
            Accessible.selected: chosen
            Rectangle {
                visible: rowItem.chosen
                width: 3
                height: parent.height
                color: Theme.accent
            }
            Rectangle {
                visible: list.activeFocus && list.currentIndex === rowItem.index
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.focus
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8
                Rectangle {
                    implicitWidth: 14
                    implicitHeight: 14
                    color: rowItem.primary
                    border.color: rowItem.outline
                    border.width: 3
                    radius: 2
                    // A thin edge so a swatch like the panel still shows.
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: Theme.control
                        opacity: 0.6
                        radius: 2
                    }
                }
                Label {
                    text: rowItem.name
                    font.italic: rowItem.pending
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Rectangle {
                    objectName: "styleUnapplied"
                    visible: manager.dirty && manager.editingStore === list.store && rowItem.chosen
                    implicitWidth: 6
                    implicitHeight: 6
                    radius: 3
                    color: Theme.accent
                    ToolTip.visible: unappliedMouse.containsMouse
                    ToolTip.text: qsTr("Changes not applied")
                    MouseArea { id: unappliedMouse; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true }
                }
                Label {
                    visible: !list.store && !rowItem.pending
                    text: rowItem.lines
                    color: rowItem.lines > 0 ? Theme.muted : Theme.disabled
                    font: manager.fontAt(12)
                }
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: mouse => {
                    list.forceActiveFocus()
                    if (!rowItem.pending)
                        list.pickIndex(rowItem.index, mouse.modifiers)
                }
                // Legacy's double click edited: the editor already shows it.
                onDoubleClicked: if (!rowItem.pending) manager.focusEditor()
            }
        }
    }
    // A list's tools: New, Duplicate, Delete, Move up, Move down, Load from
    // a file…, and the ⋯ menu (Move to top, Move to bottom, Sort by name,
    // and for the file Delete unused styles).
    component ListTools: RowLayout {
        id: tools
        required property bool store
        readonly property var selected: store ? manager.storeSelected : manager.assSelected
        readonly property string prefix: store ? "store" : "ass"
        spacing: 2
        IconToolButton {
            objectName: tools.prefix + "New"; iconRole: "add"; text: qsTr("New style")
            onClicked: manager.startNew(tools.store, false)
        }
        IconToolButton {
            objectName: tools.prefix + "Copy"; iconRole: "duplicate"; text: qsTr("Duplicate")
            enabled: tools.selected.length > 0
            onClicked: manager.startNew(tools.store, true)
        }
        IconToolButton {
            objectName: tools.prefix + "Delete"; iconRole: "delete"; text: qsTr("Delete")
            enabled: tools.selected.length > 0
            onClicked: manager.deleteSelected(tools.store)
        }
        ToolSeparator { padding: 2 }
        IconToolButton {
            objectName: tools.prefix + "MoveUp"; iconRole: "move-up"; text: qsTr("Move up")
            tip: qsTr("Move up (Alt+Up)")
            enabled: tools.selected.length > 0
            onClicked: manager.moveSelected(tools.store, 1)
        }
        IconToolButton {
            objectName: tools.prefix + "MoveDown"; iconRole: "move-down"; text: qsTr("Move down")
            tip: qsTr("Move down (Alt+Down)")
            enabled: tools.selected.length > 0
            onClicked: manager.moveSelected(tools.store, 2)
        }
        ToolSeparator { padding: 2 }
        IconToolButton {
            id: loadButton
            objectName: tools.store ? "loadToStore" : "loadToAss"
            iconRole: "import"
            text: qsTr("Load from a file…")
            onClicked: manager.loadFrom(tools.store, loadButton)
        }
        Item { Layout.fillWidth: true }
        IconToolButton {
            id: moreButton
            objectName: tools.prefix + "More"
            iconRole: "panel-menu"
            text: qsTr("More")
            onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.implicitWidth, moreButton.height)
        }
        ShellMenu {
            id: moreMenu
            objectName: tools.prefix + "MoreMenu"
            ShellMenuItem {
                iconRole: "move-to-top"
                text: qsTr("Move to top")
                enabled: tools.selected.length > 0
                onTriggered: manager.moveSelected(tools.store, 0)
            }
            ShellMenuItem {
                iconRole: "move-to-bottom"
                text: qsTr("Move to bottom")
                enabled: tools.selected.length > 0
                onTriggered: manager.moveSelected(tools.store, 3)
            }
            MenuSeparator {}
            Component.onCompleted: if (tools.store) removeItem(cleanItem)
            ShellMenuItem {
                iconRole: "sort"
                text: qsTr("Sort by name")
                onTriggered: manager.sortList(tools.store)
            }
            ShellMenuItem {
                id: cleanItem
                iconRole: "clear"
                objectName: "assClean"
                text: qsTr("Delete unused styles")
                onTriggered: manager.deleteUnused()
            }
        }
    }

    // --- The window -------------------------------------------------------------
    RowLayout {
        id: body
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        // The sidebar: the filter, the catalog and its Styles, the copies,
        // this file's Styles. It narrows to its icon strips' width (260
        // pixels here) before the editor has to scroll sideways (1280 x 800
        // at 150 %); the copies' texts, wider in some fonts, wrap instead.
        Rectangle {
            objectName: "styleSidebar"
            readonly property real narrowest: Math.max(260, Math.ceil(storeTools.implicitWidth) + 24)
            color: Theme.background
            Layout.preferredWidth: Math.max(narrowest, Math.min(300, body.width - 1 - editorScroll.narrowest
                                                                         - (editorScroll.width - editorScroll.availableWidth)))
            Layout.minimumWidth: narrowest
            Layout.fillHeight: true
            implicitHeight: side.implicitHeight + 24
            ColumnLayout {
                id: side
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                TextField {
                    id: filterField
                    objectName: "styleFilter"
                    placeholderText: qsTr("Filter styles")
                    Accessible.name: qsTr("Filter styles")
                    leftPadding: 28
                    Layout.fillWidth: true
                    // Enter and Down go to the first Style shown, this file's
                    // list first (Enter is not the window's OK here).
                    Keys.onReturnPressed: manager.fromFilter()
                    Keys.onEnterPressed: manager.fromFilter()
                    Keys.onDownPressed: manager.fromFilter()
                    Icon {
                        iconRole: "search"
                        color: Theme.muted
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                RowLayout {
                    spacing: 4
                    Layout.topMargin: 6
                    Label {
                        text: qsTr("Catalog")
                        font: manager.fontAt(13, Font.DemiBold)
                        Layout.rightMargin: 6
                    }
                    ComboBox {
                        id: catalogBox
                        objectName: "styleCatalog"
                        Layout.fillWidth: true
                        model: manager.styles.catalogs
                        currentIndex: manager.styles.catalogs.indexOf(manager.styles.catalog)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Style catalog")
                        // Named as its tooltip, not by the catalog it shows.
                        Accessible.name: qsTr("Style catalog")
                        onActivated: i => {
                            const name = model[i]
                            currentIndex = Qt.binding(() => manager.styles.catalogs.indexOf(manager.styles.catalog))
                            manager.chooseCatalog(name)
                        }
                    }
                    IconToolButton {
                        id: newCatalogButton
                        objectName: "newCatalog"
                        iconRole: "add"
                        text: qsTr("New catalog")
                        onClicked: newCatalogPopover.openFor(newCatalogButton)
                    }
                    IconToolButton {
                        objectName: "deleteCatalog"
                        iconRole: "delete"
                        text: qsTr("Delete catalog")
                        enabled: manager.styles.catalog !== "Default"
                        onClicked: manager.askDeleteCatalog()
                    }
                }
                ListTools { id: storeTools; objectName: "storeCommands"; store: true; Layout.fillWidth: true }
                Frame {
                    padding: 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The two lists share the height evenly.
                    Layout.preferredHeight: 0
                    Layout.minimumHeight: 80
                    background: Rectangle { color: Theme.field; border.color: Theme.line }
                    StyleList {
                        id: storeList
                        objectName: "storeStyles"
                        store: true
                        anchors.fill: parent
                    }
                }
                Flow {
                    id: copyRow
                    spacing: 6
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    IconTextButton {
                        objectName: "addToAss"
                        iconRole: "copy-to-ass"
                        text: qsTr("Add to file")
                        tip: qsTr("Copy the selected catalog styles into this file")
                        enabled: manager.storeSelected.length > 0 && manager.styles.available
                        onClicked: manager.copyToFile(false)
                    }
                    IconTextButton {
                        objectName: "addToStore"
                        iconRole: "copy-to-storage"
                        text: qsTr("Add to catalog")
                        tip: qsTr("Copy the selected file styles into the catalog")
                        enabled: manager.assSelected.length > 0
                        onClicked: manager.copyToCatalog()
                    }
                    IconToolButton {
                        objectName: "addToAllAss"
                        iconRole: "copy-to-all-ass"
                        text: qsTr("Add to all open files")
                        tip: qsTr("Copy the selected catalog styles into every open file")
                        enabled: manager.storeSelected.length > 0
                        onClicked: manager.copyToFile(true)
                    }
                }
                Label {
                    text: qsTr("This file")
                    font: manager.fontAt(13, Font.DemiBold)
                    Layout.topMargin: 6
                }
                ListTools { objectName: "assCommands"; store: false; enabled: manager.styles.available; Layout.fillWidth: true }
                Frame {
                    padding: 1
                    enabled: manager.styles.available
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0 // as the catalog's
                    Layout.minimumHeight: 80
                    background: Rectangle { color: Theme.field; border.color: Theme.line }
                    StyleList {
                        id: assList
                        objectName: "assStyles"
                        store: false
                        anchors.fill: parent
                    }
                    Label {
                        visible: !manager.styles.available
                        anchors.centerIn: parent
                        width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        text: qsTr("This tab has no ASS styles.")
                    }
                }
            }
        }
        Rectangle {
            color: Theme.line
            implicitWidth: 1
            Layout.fillHeight: true
        }
        // StyleChange: the editor, on the selected Styles, under its preview.
        ColumnLayout {
            spacing: 0
            enabled: manager.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 680
            Layout.minimumWidth: 420
            // The preview and its text, pinned above the fields.
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Layout.margins: 24
                Layout.topMargin: 12
                Layout.bottomMargin: 12
                Image {
                    id: preview
                    objectName: "stylePreview"
                    Layout.fillWidth: true
                    // SV1: drawn with the screen's pixels; the layout takes
                    // its size in the window's units (Image ignores the
                    // image's pixel ratio).
                    Layout.preferredWidth: manager.styles.previewScale > 1 ? implicitWidth / manager.styles.previewScale : -1
                    Layout.preferredHeight: 84
                    cache: false
                    source: manager.editing ? "image://stylepreview/" + manager.styles.previewKey : ""
                    onWidthChanged: manager.updatePreview()
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: Theme.line
                    }
                    Label {
                        objectName: "stylePreviewNote"
                        visible: manager.multi
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        padding: 2
                        leftPadding: 6
                        rightPadding: 6
                        color: "#FFFFFF"
                        font: manager.fontAt(11)
                        background: Rectangle { color: "#B0000000"; radius: 2 }
                        text: qsTr("%n styles: the preview shows %1", "", manager.editedRows.length).arg(manager.values.name || "")
                    }
                }
                // Legacy STYLE_PREVIEW_TEXT, edited by clicking the preview.
                // SY3: kept when edited, as legacy's StylePreview kept it
                // (StylePreview.cpp:136-141).
                TextField {
                    id: previewText
                    objectName: "stylePreviewText"
                    text: manager.catalogs.previewText
                    Accessible.name: qsTr("Preview text")
                    leftPadding: 28
                    Layout.fillWidth: true
                    onTextEdited: manager.updatePreview()
                    onEditingFinished: manager.catalogs.setPreviewText(text)
                    // Enter keeps the text (it is not the window's OK here).
                    Keys.onReturnPressed: manager.catalogs.setPreviewText(text)
                    Keys.onEnterPressed: manager.catalogs.setPreviewText(text)
                    Icon {
                        iconRole: "edit"
                        color: Theme.muted
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            Rectangle {
                color: Theme.line
                implicitHeight: 1
                Layout.fillWidth: true
            }
            // A window too short for the fields scrolls them, with a
            // visible scroll bar; the preview and the buttons stay.
            ScrollView {
                id: editorScroll
                objectName: "styleEditorScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                // As wide as the view, or the fields' narrowest (a screen
                // narrower than 1280 x 800 at 150 % scrolls them sideways).
                // The grid reports it once measured (a layout measures
                // itself while laid out: read in this binding, it loops).
                property real narrowest: 0
                contentWidth: Math.max(narrowest, availableWidth)
                // Its width is the layout's, never its content's: a view sized
                // by what it scrolls would loop through contentWidth.
                implicitWidth: 0
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                ScrollBar.horizontal.policy: contentWidth > availableWidth + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                GridLayout {
                    id: editor
                    objectName: "styleEditor"
                    width: editorScroll.contentWidth - 48 - (editorScroll.ScrollBar.vertical.visible ? editorScroll.ScrollBar.vertical.width : 0)
                    x: 24
                    // After the layout pass that measured it.
                    onImplicitWidthChanged: Qt.callLater(() => editorScroll.narrowest = Layout.minimumWidth + 48)
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 4
                    uniformCellWidths: true
                    // The fields as the Style's values; a field showing
                    // "Multiple values" is left empty.
                    function load(m) {
                        const show = (key, text) => manager.isMixed(key) ? "" : String(text)
                        nameField.text = manager.multi ? "" : m.name
                        fontField.editText = show("fontname", m.fontname)
                        sizeField.text = show("fontsize", m.fontsize)
                        boldBox.checked = m.bold && !manager.isMixed("bold")
                        italicBox.checked = m.italic && !manager.isMixed("italic")
                        underlineBox.checked = m.underline && !manager.isMixed("underline")
                        strikeBox.checked = m.strikeOut && !manager.isMixed("strikeOut")
                        colours.itemAt(0).setColour(m.primary)
                        colours.itemAt(1).setColour(m.secondary)
                        colours.itemAt(2).setColour(m.outline)
                        colours.itemAt(3).setColour(m.back)
                        outlineField.text = show("outlineWidth", m.outlineWidth)
                        shadowField.text = show("shadow", m.shadow)
                        scaleXField.text = show("scaleX", m.scaleX)
                        scaleYField.text = show("scaleY", m.scaleY)
                        angleField.text = show("angle", m.angle)
                        spacingField.text = show("spacing", m.spacing)
                        borderBox.checkState = manager.isMixed("borderStyle") ? Qt.PartiallyChecked
                                             : m.borderStyle ? Qt.Checked : Qt.Unchecked
                        const at = [7, 8, 9, 4, 5, 6, 1, 2, 3].indexOf(Number(m.alignment))
                        for (let i = 0; i < 9; ++i)
                            alignments.itemAt(i).checked = !manager.isMixed("alignment") && i === at
                        marginLeft.text = show("marginLeft", m.marginLeft)
                        marginRight.text = show("marginRight", m.marginRight)
                        marginVertical.text = show("marginVertical", m.marginVertical)
                        // An unknown encoding shows "1 - Default" (legacy) and
                        // stays as it was until another is chosen.
                        encodings.code = String(m.encoding)
                        const enc = encodings.model.findIndex(e => e.startsWith(m.encoding + " "))
                        encodings.currentIndex = manager.isMixed("encoding") ? -1 : enc < 0 ? 1 : enc
                    }
                    // The values to commit: a field still showing "Multiple
                    // values" keeps the value it opened with.
                    function values() {
                        let alignment = String(manager.values.alignment)
                        for (let i = 0; i < 9; ++i)
                            if (alignments.itemAt(i).checked)
                                alignment = String([7, 8, 9, 4, 5, 6, 1, 2, 3][i])
                        const v = {
                            name: manager.multi ? manager.values.name : nameField.text, fontname: fontField.editText,
                            fontsize: sizeField.text,
                            primary: colours.itemAt(0).colour, secondary: colours.itemAt(1).colour,
                            outline: colours.itemAt(2).colour, back: colours.itemAt(3).colour,
                            bold: boldBox.checked, italic: italicBox.checked, underline: underlineBox.checked,
                            strikeOut: strikeBox.checked, outlineWidth: outlineField.text, shadow: shadowField.text,
                            scaleX: scaleXField.text, scaleY: scaleYField.text, angle: angleField.text,
                            spacing: spacingField.text, borderStyle: borderBox.checkState === Qt.Checked, alignment: alignment,
                            marginLeft: marginLeft.text, marginRight: marginRight.text, marginVertical: marginVertical.text,
                            encoding: encodings.code
                        }
                        for (const key of manager.mixed)
                            v[key] = manager.values[key]
                        return v
                    }

                    // Name
                    SectionTitle { text: qsTr("Name"); Layout.topMargin: 16 }
                    TextField {
                        id: nameField
                        objectName: "styleName"
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                        enabled: !manager.multi
                        placeholderText: manager.multi ? qsTr("Multiple values") : ""
                        font.italic: manager.multi
                        Accessible.name: qsTr("Name")
                        validator: RegularExpressionValidator { regularExpression: /[^,]*/ } // legacy excludes ","
                        onTextEdited: manager.edited("name")
                        Rectangle {
                            visible: manager.nameProblem !== ""
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Theme.danger
                            border.width: 2
                            radius: 2
                        }
                    }
                    // SY1-name-inline-error: an empty or taken name, under the field.
                    Label {
                        objectName: "styleNameProblem"
                        visible: manager.nameProblem !== ""
                        text: manager.nameProblem
                        color: Theme.danger
                        wrapMode: Text.Wrap
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }
                    // SY1-rename-inline: the old question, as a check box.
                    CheckBox {
                        id: renameBox
                        objectName: "styleRenameLines"
                        visible: manager.editing && !manager.editingStore && !manager.pendingNew && !manager.multi
                                 && manager.changedKeys.name === true && manager.nameProblem === "" && manager.editedLines > 0
                        checked: true
                        text: manager.editedLines === 1 ? qsTr("Rename it in the line that uses it")
                                                        : qsTr("Rename it in the %n lines that use it", "", manager.editedLines)
                        Layout.columnSpan: 4
                    }

                    // Font
                    SectionTitle { text: qsTr("Font") }
                    FieldLabel { text: qsTr("Family"); key: "fontname"; Layout.columnSpan: 3 }
                    FieldLabel { text: qsTr("Size"); key: "fontsize" }
                    ComboBox {
                        id: fontField
                        objectName: "styleFont"
                        editable: true
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Font")
                        font.italic: manager.isMixed("fontname")
                        onEditTextChanged: manager.updatePreview()
                        Connections {
                            target: fontField.contentItem
                            function onTextEdited() { manager.edited("fontname") }
                        }
                        onActivated: manager.edited("fontname")
                        onHighlightedIndexChanged: if (popup.visible) manager.updatePreview()
                        // The box's text field draws no placeholder of its own.
                        Label {
                            objectName: "styleFontMixed"
                            z: 2 // over the box's text field
                            visible: manager.isMixed("fontname") && fontField.editText.length === 0
                            x: fontField.contentItem.x + fontField.contentItem.leftPadding
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Multiple values")
                            font: fontField.font
                            color: fontField.palette.placeholderText
                        }
                        // The family highlighted in the open list, or "".
                        function browsedFont() {
                            return popup.visible && highlightedIndex >= 0 && model && highlightedIndex < model.length
                                   ? model[highlightedIndex] : ""
                        }
                        Connections {
                            target: fontField.popup
                            function onVisibleChanged() { manager.updatePreview() }
                        }
                        // A refresh of the fonts: each row's face asked again.
                        Connections {
                            target: manager.catalogs
                            function onFontsChanged() { FontFaces.forget() }
                        }
                        // SY3: each family in its own face. The list makes
                        // rows only for what it shows, so a large font set
                        // costs no more than a screenful of faces.
                        delegate: FontFamilyRow {
                            required property string modelData
                            required property int index
                            objectName: "styleFontRow"
                            family: modelData
                            width: ListView.view ? ListView.view.width : implicitWidth
                            highlighted: fontField.highlightedIndex === index
                            hoverEnabled: fontField.hoverEnabled
                        }
                    }
                    StyleField { id: sizeField; objectName: "styleSize"; key: "fontsize"; Accessible.name: qsTr("Size") }
                    FieldLabel { text: qsTr("Font catalog"); Layout.columnSpan: 2 }
                    FieldLabel { text: qsTr("Emphasis"); keys: ["bold", "italic", "underline", "strikeOut"]; resetName: "emphasis" }
                    FieldLabel { text: qsTr("Text encoding"); key: "encoding" }
                    // Y6: the font catalogs and the Filter (StyleChange's filtersizer).
                    FontCatalogBar {
                        id: fontCatalogBar
                        property bool made: false
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Layout.preferredWidth: 120
                        catalogs: manager.catalogs
                        fontName: fontField.editText
                        // HikariChoice::PutArray keeps the typed font.
                        onListMade: {
                            const text = fontField.editText
                            fontField.model = fonts
                            fontField.editText = text
                        }
                    }
                    // Bold, Italic, Underline and Strikethrough as toggles.
                    Rectangle {
                        objectName: "styleEmphasis"
                        color: "transparent"
                        border.color: Theme.control
                        radius: 2
                        implicitWidth: emphasisRow.implicitWidth + 2
                        implicitHeight: emphasisRow.implicitHeight + 2
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.minimumWidth: implicitWidth
                        Row {
                            id: emphasisRow
                            x: 1
                            y: 1
                            EmphasisToggle { id: boldBox; objectName: "styleBold"; key: "bold"; iconRole: "tag-bold"; text: qsTr("Bold") }
                            EmphasisToggle { id: italicBox; objectName: "styleItalic"; key: "italic"; iconRole: "tag-italic"; text: qsTr("Italic") }
                            EmphasisToggle { id: underlineBox; objectName: "styleUnderline"; key: "underline"; iconRole: "tag-underline"; text: qsTr("Underline") }
                            EmphasisToggle { id: strikeBox; objectName: "styleStrikeout"; key: "strikeOut"; iconRole: "tag-strikeout"; text: qsTr("Strikethrough") }
                        }
                    }
                    ComboBox {
                        id: encodings
                        objectName: "styleEncoding"
                        // The encoding written: the one chosen, else the Style's own.
                        property string code: "1"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        Accessible.name: qsTr("Text encoding")
                        displayText: currentIndex < 0 ? qsTr("Multiple values") : currentText
                        onActivated: { code = currentText.split(" ")[0]; manager.edited("encoding") }
                        model: [qsTr("0 - ANSI"), qsTr("1 - Default"), qsTr("2 - Symbol"), qsTr("77 - Mac"), qsTr("128 - Japanese"),
                                qsTr("129 - Hangul"), qsTr("130 - Johab"), qsTr("134 - Chinese GB2312"), qsTr("135 - Chinese BIG5"),
                                qsTr("161 - Greek"), qsTr("162 - Turkish"), qsTr("163 - Vietnamese"), qsTr("177 - Hebrew"),
                                qsTr("178 - Arabic"), qsTr("186 - Baltic"), qsTr("204 - Russian"), qsTr("222 - Thai"),
                                qsTr("238 - Central European (Polish)"), qsTr("255 - OEM")]
                    }

                    // Colours and transparency: legacy's kolgrid
                    // (StyleChange.cpp:146-171), one column each.
                    SectionTitle { text: qsTr("Colours and transparency") }
                    Repeater {
                        id: labelsOfColours
                        model: [[qsTr("Primary"), "primary"], [qsTr("Secondary"), "secondary"],
                                [qsTr("Border"), "outline"], [qsTr("Shadow"), "back"]]
                        FieldLabel {
                            required property var modelData
                            text: modelData[0]
                            key: modelData[1]
                        }
                    }
                    Repeater {
                        id: colours
                        model: [[qsTr("Primary"), "primary"], [qsTr("Secondary"), "secondary"],
                                [qsTr("Border"), "outline"], [qsTr("Shadow"), "back"]]
                        ColourField {
                            required property var modelData
                            required property int index
                            number: index + 1
                            name: modelData[0]
                            key: modelData[1]
                        }
                    }

                    // Shape and position: legacy's labelled boxes and
                    // Alignment (StyleChange.cpp:173-252) as one grid, each
                    // field under its label; Alignment beside Angle and the
                    // margins.
                    SectionTitle { text: qsTr("Shape and position") }
                    GridLayout {
                        id: geometry
                        objectName: "styleGeometry"
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 12
                        rowSpacing: 4
                        uniformCellWidths: true
                        FieldLabel { Layout.row: 0; Layout.column: 0; text: qsTr("Border"); key: "outlineWidth" }
                        FieldLabel { Layout.row: 0; Layout.column: 1; text: qsTr("Shadow"); key: "shadow" }
                        FieldLabel { Layout.row: 0; Layout.column: 2; text: qsTr("Scale X"); key: "scaleX" }
                        FieldLabel { Layout.row: 0; Layout.column: 3; text: qsTr("Scale Y"); key: "scaleY" }
                        StyleField { id: outlineField; objectName: "styleOutline"; key: "outlineWidth"; Layout.row: 1; Layout.column: 0; Accessible.name: qsTr("Border width") }
                        StyleField { id: shadowField; objectName: "styleShadow"; key: "shadow"; Layout.row: 1; Layout.column: 1; Accessible.name: qsTr("Shadow distance") }
                        StyleField { id: scaleXField; objectName: "styleScaleX"; key: "scaleX"; unit: "%"; Layout.row: 1; Layout.column: 2; Accessible.name: qsTr("Scale X") }
                        StyleField { id: scaleYField; objectName: "styleScaleY"; key: "scaleY"; unit: "%"; Layout.row: 1; Layout.column: 3; Accessible.name: qsTr("Scale Y") }
                        FieldLabel { Layout.row: 2; Layout.column: 0; text: qsTr("Angle"); key: "angle" }
                        FieldLabel { Layout.row: 2; Layout.column: 1; text: qsTr("Spacing"); key: "spacing" }
                        FieldLabel { Layout.row: 2; Layout.column: 2; text: qsTr("Border type"); key: "borderStyle" }
                        FieldLabel { Layout.row: 2; Layout.column: 3; text: qsTr("Alignment"); key: "alignment" }
                        StyleField { id: angleField; objectName: "styleAngle"; key: "angle"; unit: "°"; Layout.row: 3; Layout.column: 0; Accessible.name: qsTr("Angle") }
                        StyleField { id: spacingField; objectName: "styleSpacing"; key: "spacing"; Layout.row: 3; Layout.column: 1; Accessible.name: qsTr("Spacing") }
                        CheckBox {
                            id: borderBox
                            objectName: "styleOpaqueBox"
                            Layout.row: 3; Layout.column: 2
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: qsTr("Opaque box")
                            Accessible.description: checkState === Qt.PartiallyChecked ? qsTr("Multiple values") : ""
                            onToggled: manager.edited("borderStyle")
                        }
                        // The 3 × 3 alignment grid (the numpad's places).
                        Rectangle {
                            objectName: "styleAlignment"
                            Layout.row: 3; Layout.column: 3; Layout.rowSpan: 3
                            Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                            implicitWidth: alignGrid.implicitWidth + 2
                            implicitHeight: alignGrid.implicitHeight + 2
                            color: Theme.field
                            border.color: Theme.control
                            radius: 2
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Alignment")
                            // No place checked: the Styles differ.
                            Accessible.description: manager.isMixed("alignment") ? qsTr("Multiple values") : ""
                            ButtonGroup { id: alignmentGroup }
                            Grid {
                                id: alignGrid
                                x: 1
                                y: 1
                                columns: 3
                                Repeater {
                                    id: alignments
                                    model: ["7", "8", "9", "4", "5", "6", "1", "2", "3"]
                                    AbstractButton {
                                        id: place
                                        required property string modelData
                                        required property int index
                                        checkable: true
                                        ButtonGroup.group: alignmentGroup
                                        implicitWidth: 22
                                        implicitHeight: 22
                                        text: modelData
                                        Accessible.role: Accessible.RadioButton
                                        Accessible.name: qsTr("Alignment %1").arg(modelData)
                                        ToolTip.visible: hovered
                                        ToolTip.text: [qsTr("Top left"), qsTr("Top"), qsTr("Top right"), qsTr("Left"), qsTr("Middle"),
                                                       qsTr("Right"), qsTr("Bottom left"), qsTr("Bottom"), qsTr("Bottom right")][index]
                                        onToggled: manager.edited("alignment")
                                        Keys.onPressed: event => {
                                            const step = { [Qt.Key_Left]: -1, [Qt.Key_Right]: 1, [Qt.Key_Up]: -3, [Qt.Key_Down]: 3 }[event.key]
                                            if (step === undefined)
                                                return
                                            const next = place.index + step
                                            if (next >= 0 && next < 9 && (Math.abs(step) === 3 || Math.floor(next / 3) === Math.floor(place.index / 3))) {
                                                const b = alignments.itemAt(next)
                                                b.checked = true
                                                b.forceActiveFocus(Qt.TabFocusReason)
                                                manager.edited("alignment")
                                            }
                                            event.accepted = true
                                        }
                                        background: Rectangle {
                                            color: place.hovered ? Theme.raised : "transparent"
                                            border.color: place.visualFocus ? Theme.focus : "transparent"
                                        }
                                        contentItem: Item {
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: place.checked ? 12 : 3
                                                height: width
                                                color: place.checked ? Theme.accent : Theme.text
                                                border.color: place.checked ? Theme.text : "transparent"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        FieldLabel { Layout.row: 4; Layout.column: 0; text: qsTr("Left margin"); key: "marginLeft" }
                        FieldLabel { Layout.row: 4; Layout.column: 1; text: qsTr("Right margin"); key: "marginRight" }
                        FieldLabel { Layout.row: 4; Layout.column: 2; text: qsTr("Vertical margin"); key: "marginVertical" }
                        StyleField { id: marginLeft; objectName: "styleMarginLeft"; key: "marginLeft"; Layout.row: 5; Layout.column: 0; Accessible.name: qsTr("Left margin") }
                        StyleField { id: marginRight; objectName: "styleMarginRight"; key: "marginRight"; Layout.row: 5; Layout.column: 1; Accessible.name: qsTr("Right margin") }
                        StyleField { id: marginVertical; objectName: "styleMarginVertical"; key: "marginVertical"; Layout.row: 5; Layout.column: 2; Accessible.name: qsTr("Vertical margin") }
                    }
                    Item { implicitHeight: 20; Layout.columnSpan: 4 }
                }
            }
        }
    }

    // One footer, whatever is selected (SY1-one-footer): Revert drops what
    // is not applied and stays; OK applies and closes; Cancel (and Esc, and
    // the title bar's Close) drops it and closes; Apply applies.
    leadingActions: [
        Button {
            objectName: "styleEditorRevert"
            text: qsTr("Revert")
            enabled: manager.dirty
            onClicked: manager.revert()
        }
    ]
    buttons: [
        PrimaryButton {
            id: okButton
            objectName: "styleEditorOk"
            text: qsTr("OK")
            enabled: manager.nameProblem === ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: manager.commit(true)
        },
        Button {
            objectName: "styleEditorCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: manager.reject()
        },
        Button {
            objectName: "styleEditorApply"
            text: qsTr("Apply")
            enabled: manager.dirty && manager.nameProblem === ""
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: manager.commit(false)
        }
    ]

    // The lists' rows: the filter's, and a New or Duplicate not yet made at
    // the end of its list.
    readonly property var storeShown: shownRows(true, storeRows, filterText, pendingNew && editingStore, nameField.text, heldRows(true))
    readonly property var assShown: shownRows(false, assRows, filterText, pendingNew && !editingStore, nameField.text, heldRows(false))
    // The Styles the editor is on stay listed whatever the filter: an edit
    // keeps its targets, and no Style it acts on is hidden.
    function heldRows(store) { return editing && !pendingNew && editingStore === store ? editedRows : [] }
    function shownRows(store, rows, filter, pending, pendingName, held) {
        const keep = Object.create(null)
        for (const r of held)
            keep[r] = true
        const out = []
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            if (filter.length === 0 || r.name.toLowerCase().indexOf(filter) >= 0 || keep[i])
                out.push({ row: i, name: r.name, primary: r.primary, outline: r.outline, lines: r.lines, pending: false })
        }
        if (pending) {
            const c = colours.count > 0 ? colours.itemAt(0).colour : { r: 255, g: 255, b: 255 }
            const o = colours.count > 2 ? colours.itemAt(2).colour : { r: 0, g: 0, b: 0 }
            out.push({ row: -1, name: pendingName, primary: Qt.rgba(c.r / 255, c.g / 255, c.b / 255, 1).toString(),
                       outline: Qt.rgba(o.r / 255, o.g / 255, o.b / 255, 1).toString(), lines: 0, pending: true })
        }
        return out
    }
    onStoreShownChanged: storeList.sync(storeShown)
    onAssShownChanged: assList.sync(assShown)
    // A row the filter hides leaves the selection, so no command acts on a
    // Style that is not shown; the editor follows, and an edit not applied
    // stays open on its Style.
    // Any other selection loses the rows the filter hides, so no command
    // acts on a Style not shown (also when the editor closes).
    onFilterTextChanged: dropHidden()
    onEditingChanged: if (!editing) dropHidden()
    function dropHidden() {
        // (The rows are read, not storeShown: its binding may not have
        // followed the filter yet.)
        for (const store of [true, false]) {
            if (heldRows(store).length > 0)
                continue
            const rows = store ? storeSelected : assSelected
            const list = store ? storeRows : assRows
            const kept = rows.filter(r => r < list.length
                                          && (filterText.length === 0 || list[r].name.toLowerCase().indexOf(filterText) >= 0))
            if (kept.length !== rows.length)
                followRows(store, kept)
        }
    }

    // Y7: StyleChange::OnAllCols (StyleChange.cpp:335-399). SY2: a left
    // click opens the colour popover at the button's colour and
    // transparency, a right click opens it with the eyedropper started;
    // COLORPICKER_SWITCH_CLICKS swaps them.
    function colourClick(number, leftClick) {
        if (colourPicker.switchClicks)
            leftClick = !leftClick
        const column = colours.itemAt(number - 1)
        stylePopover.openFor(column.swatch, styleColours, () => {
            colourPicker.opened("styleEditor") // DialogColorPicker::Get(this, ...)
            return styleColours.begin(number)
        }, { title: column.name, dropper: !leftClick, focus: column.swatch })
    }
    // The popover's target: one of the four colours of the edited Style.
    QtObject {
        id: styleColours
        property int number: 1
        // lastColor: what Cancel puts back.
        property var lastColour: ({ r: 0, g: 0, b: 0, a: 0 })
        property bool wasMixed: false
        function begin(n) {
            number = n
            const field = colours.itemAt(n - 1)
            const c = field.colour
            lastColour = { r: c.r, g: c.g, b: c.b, a: c.a }
            wasMixed = field.isMixed
            return lastColour
        }
        // COLOR_CHANGED: UpdateColor (737-746) sets the button, the
        // transparency and the preview.
        function changeColour(c) {
            const field = colours.itemAt(number - 1)
            field.setColour({ r: c.r, g: c.g, b: c.b, a: c.a })
            manager.edited(field.key)
            return true
        }
        // Cancel: UpdateColor(lastColor) (368-371, 395-398), and "Multiple
        // values" again where it was.
        function endDialog(accepted) {
            if (accepted)
                return
            changeColour(lastColour)
            if (wasMixed) {
                manager.unresolve(colours.itemAt(number - 1).key)
                manager.refreshChanges()
            }
        }
    }
    ColourPopover {
        id: stylePopover
        objectName: "styleColourPopover"
        picker: manager.colourPicker
    }
    // New catalog: its name, in a popover at the button
    // (SY1-catalog-popups).
    Popover {
        id: newCatalogPopover
        objectName: "newCatalogPopover"
        title: qsTr("New catalog")
        showTitle: true
        initialFocus: catalogName
        readonly property string problem: {
            const name = catalogName.text.trim()
            if (name.length === 0)
                return ""
            return manager.styles.catalogs.indexOf(name) >= 0 ? qsTr("A catalog named “%1” already exists.").arg(name) : ""
        }
        function openFor(anchorItem) {
            catalogName.text = ""
            openAt(anchorItem)
        }
        function create() {
            const name = catalogName.text.trim()
            if (name.length === 0 || problem !== "")
                return
            keep()
            manager.createCatalog(name)
        }
        TextField {
            id: catalogName
            objectName: "newCatalogName"
            placeholderText: qsTr("Catalog name")
            Accessible.name: qsTr("Catalog name")
            Layout.preferredWidth: 240
            onAccepted: newCatalogPopover.create()
        }
        Label {
            visible: newCatalogPopover.problem !== ""
            text: newCatalogPopover.problem
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.preferredWidth: 240
        }
        PrimaryButton {
            objectName: "newCatalogCreate"
            text: qsTr("Create")
            enabled: catalogName.text.trim().length > 0 && newCatalogPopover.problem === ""
            Layout.alignment: Qt.AlignRight
            onClicked: newCatalogPopover.create()
        }
    }
    // LoadStylesS: "Choose ASS file", then the Styles to load (PK1's Choose
    // styles popover, at the Load button), then the clash review.
    FileDialog {
        id: loadDialog
        property bool toStore: false
        property Item anchor: null
        property string key // listsKey when Load was clicked
        title: qsTr("Choose ASS file")
        nameFilters: [qsTr("ASS subtitle files(*.ass)") + " (*.ass)"]
        onAccepted: {
            chooseLoaded.file = selectedFile
            chooseLoaded.openFor(anchor, manager.styles.fileStyles(selectedFile), [], { actionText: qsTr("Load") })
        }
    }
    ChooseStyles {
        id: chooseLoaded
        objectName: "chooseLoadedStyles"
        property url file
        onActed: names => {
            const toStore = loadDialog.toStore
            const file = chooseLoaded.file
            const key = loadDialog.key
            if (manager.styles.listsKey() !== key) {
                manager.showError(qsTr("Copy styles"), qsTr("The styles changed while this was open. Nothing was copied."))
                return
            }
            manager.reviewClashes(manager.styles.fileClashes(toStore, file, names), names.length,
                                  toStore ? qsTr("the catalog") : qsTr("this file"), function(choices) {
                // Added at the end, or in place of a clash: the rows stay.
                manager.own(() => manager.styles.loadFromFile(toStore, file, names, choices))
                manager.followRows(toStore, toStore ? manager.storeSelected : manager.assSelected)
            }, key)
        }
    }
}
