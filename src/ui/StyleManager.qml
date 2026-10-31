pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: manager
    objectName: "styleManager"
    Component.onCompleted: IconTheme.setWindowIcon(manager, "styles")
    required property var styles
    required property var catalogs
    required property ColourPickerController colourPicker
    required property var app
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
    property var values: ({})
    property var mixed: []
    property var openedMixed: []
    property var resolved: []
    property bool pendingNew: false
    property var changedKeys: ({})
    readonly property bool dirty: editing && (pendingNew || Object.keys(changedKeys).length > 0)
    readonly property var editedRows: editingStore ? storeSelected : assSelected
    readonly property bool multi: editing && !pendingNew && editedRows.length > 1
    property var storeRows: []
    property var assRows: []
    property string listedCatalog: ""
    // MUST KILL: ownChange is set by own() because the controller emits its change before the action returns
    property bool ownChange: false
    property bool awaitingSwitch: false
    readonly property string filterText: filterField.text.trim().toLowerCase()

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
            open()
            guard(go)
            return
        }
        refresh()
        const row = rowNamed(false, styleName)
        if (row < 0)
            choose(false, shownOf(false, [0]))
        open()
        go()
    }
    function showAt(row) {
        if (!editing) {
            refresh()
            choose(false, row >= 0 && row < assRows.length ? [row] : shownOf(false, [0]))
        }
        open()
    }
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
        requestActivate()
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
    // On Wayland the window hides while its frame is remade; that is no closing.
    function shown() { return visible || WindowFrame.remaking(manager) }
    onVisibleChanged: {
        if (shown())
            return
        styles.saveCatalog()
        endEditing()
    }

    function refresh() {
        styles.refreshLists()
        reloadRows()
    }
    function reloadRows() {
        storeRows = styles.styleRows(true)
        assRows = styles.styleRows(false)
        listedCatalog = styles.catalog
    }
    function own(action) {
        ownChange = true
        try {
            return action()
        } finally {
            ownChange = false
        }
    }
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
    function rebase(store, row, rows) {
        const edits = editor.values()
        const kept = Object.keys(changedKeys)
        const map = styles.beginEdit(store, row)
        if (map.name === undefined)
            return
        const fields = rows.length > 1 ? styles.mixedFields(store, rows) : []
        if (!Object.keys(map).some(k => !same(map[k], values[k])) && sameRows(fields.slice().sort(), openedMixed.slice().sort()))
            return
        values = map
        openedMixed = fields
        mixed = fields.filter(k => kept.indexOf(k) < 0)
        resolved = kept.filter(k => fields.indexOf(k) >= 0)
        const v = Object.assign({}, map)
        for (const k of kept)
            v[k] = edits[k]
        editor.load(v)
        refreshChanges()
        updatePreview()
    }
    function catalogMoved() {
        if (!dirty) {
            catalogShown()
            return
        }
        if (messages.asking)
            return
        ask({
            title: qsTr("Edit style"),
            text: qsTr("Apply the changes to “%1”?").arg(pendingNew ? nameField.text : values.name),
            detail: qsTr("The catalog “%1” is shown now; the changes go to “%2”.").arg(styles.catalog, styles.editCatalog()),
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
            if (manager.styles.listsKey() !== key)
                return
            if (id === "apply") {
                if (manager.commit(false))
                    next()
            } else if (id === "discard") {
                manager.revert()
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
    function sortList(store) {
        const keys = keysOf(store, store ? storeSelected : assSelected)
        own(() => styles.sortStyles(store))
        followRows(store, sorted(rowsOfKeys(store, keys)))
    }
    function deleteUnused() {
        const done = () => {
            const keys = keysOf(false, assSelected)
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

    // MUST KILL: fieldNames decodes the controller's field bits by magic numbers
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
            text: qsTr("%1 of %2 styles already exist in %3.").arg(listed.length, total, into),
            detail: same > 0 ? qsTr("%n more are the same as the ones there and are left as they are.", "", same) : "",
            rows: listed.map(c => ({ name: c.name, occurrence: c.occurrence, difference: manager.fieldNames(c.fields), suggestion: c.suggestion })),
            added: total - clashes.length,
            acceptText: count => qsTr("Copy %n style(s)", "", count)
        }, results => {
            if (!results)
                return
            if (manager.styles.listsKey() !== since) {
                manager.showError(qsTr("Copy styles"), qsTr("The styles changed while this was open. Nothing was copied."))
                return
            }
            done(results)
        })
    }
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
    Loader {
        id: clashReviewLoader
        active: false
        sourceComponent: ClashReview {
            objectName: "styleClashReview"
            dialogId: "styleClashes"
            settings: manager.app.settings
        }
    }
    function ask(options, done) { messages.ask(options, done) }
    function showError(title, text) {
        ask({ title: title, text: text, severity: "error" })
    }
    MessageHost {
        id: messages
        objectName: "styleMessages"
        settings: manager.app.settings
    }

    function chooseCatalog(name) {
        guard(() => {
            if (manager.own(() => styles.chooseCatalog(name)))
                manager.catalogShown()
        })
    }
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

    function beginEditing(map, store) { beginMixed(map, store, []) }
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
        if (!fontCatalogBar.made) {
            fontCatalogBar.made = true
            fontCatalogBar.reset()
        }
        // MUST KILL: editing must be set before editor.load(): a multiple selection leaves the name empty
        editing = true
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
        for (const key of resolved)
            out[key] = true
        changedKeys = out
    }
    function unresolve(key) {
        if (resolved.indexOf(key) < 0)
            return
        resolved = resolved.filter(k => k !== key)
        if (!isMixed(key))
            mixed = mixed.concat([key])
    }
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
    function commitValues(v) {
        const many = multi
        return own(() => styles.commitEdit(v, many ? editedRows : [], many, renameBox.visible && renameBox.checked,
                                           many ? resolved : []))
    }
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
                                 qsTr("“%1” keeps its name; the other changes were applied. %2").arg(values.name, problem),
                                 { details: refused })
                return
            }
        }
        if (problem !== "")
            dropped(name, problem)
    }
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

    Connections {
        target: manager.styles
        function onChanged() { manager.reloadFollowing() }
        function onTargetEdited(switched) {
            if (!manager.shown() || manager.awaitingSwitch)
                return
            if (!switched) {
                manager.reloadFollowing()
                return
            }
            const catalogMoves = manager.styles.followedCatalog() !== manager.styles.catalog
            const onStore = manager.editing && manager.editingStore
            const onFile = manager.editing && !manager.editingStore
            const shown = () => {
                manager.refresh()
                if (onFile)
                    manager.choose(false, manager.shownOf(false, [0]))
                else if (onStore && catalogMoves)
                    manager.choose(true, manager.shownOf(true, [0]))
                else
                    manager.assSelected = []
            }
            if (!manager.dirty || (onStore && !catalogMoves)) {
                shown()
                return
            }
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
                if (id === "apply")
                    manager.applyAway()
                manager.awaitingSwitch = false
                manager.revert()
                shown()
            })
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: { filterField.forceActiveFocus(); filterField.selectAll() }
    }
    function cycleRegions(step) {
        const regions = [storeList, assList, null].filter(r => r === null || r.enabled)
        let at = regions.findIndex(r => r !== null && r.activeFocus)
        if (at < 0)
            at = regions.length - 1
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

    component SectionTitle: Label {
        font: manager.fontAt(13, Font.DemiBold)
        Layout.columnSpan: 4
        Layout.topMargin: 12
    }
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
                color: Color.rgba(colourField.colour.r / 255, colourField.colour.g / 255, colourField.colour.b / 255, 1)
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
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: manager.colourClick(colourField.number, false)
            }
        }
        TextField {
            id: alphaField
            objectName: "styleAlpha" + colourField.number
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
        // Updated in place: a new model would reset the view's position.
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
                    return
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
                onDoubleClicked: if (!rowItem.pending) manager.focusEditor()
            }
        }
    }
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
                iconRole: "delete"
                objectName: "assClean"
                text: qsTr("Delete unused styles")
                onTriggered: manager.deleteUnused()
            }
        }
    }

    RowLayout {
        id: body
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
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
                    Layout.preferredHeight: 0
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
        ColumnLayout {
            spacing: 0
            enabled: manager.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 680
            Layout.minimumWidth: 420
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
                    // Image ignores the provider's pixel ratio, so the layout sets its size in window units.
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
                TextField {
                    id: previewText
                    objectName: "stylePreviewText"
                    text: manager.catalogs.previewText
                    Accessible.name: qsTr("Preview text")
                    leftPadding: 28
                    Layout.fillWidth: true
                    onTextChanged: manager.updatePreview()
                    onEditingFinished: manager.catalogs.setPreviewText(text)
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
            ScrollView {
                id: editorScroll
                objectName: "styleEditorScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                // Set from the grid after layout: reading its minimum width in this binding loops.
                property real narrowest: 0
                contentWidth: Math.max(narrowest, availableWidth)
                // Never sized by its content: that would loop through contentWidth.
                implicitWidth: 0
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                ScrollBar.horizontal.policy: contentWidth > availableWidth + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                GridLayout {
                    id: editor
                    objectName: "styleEditor"
                    width: editorScroll.contentWidth - 48 - (editorScroll.ScrollBar.vertical.visible ? editorScroll.ScrollBar.vertical.width : 0)
                    x: 24
                    onImplicitWidthChanged: Qt.callLater(() => editorScroll.narrowest = Layout.minimumWidth + 48)
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 4
                    uniformCellWidths: true
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
                        // MUST KILL: an unknown encoding shows as "1 - Default" while encodings.code keeps it
                        encodings.code = String(m.encoding)
                        const enc = encodings.model.findIndex(e => e.startsWith(m.encoding + " "))
                        encodings.currentIndex = manager.isMixed("encoding") ? -1 : enc < 0 ? 1 : enc
                    }
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
                        validator: RegularExpressionValidator { regularExpression: /[^,]*/ } // ASS Style lines are comma-separated
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
                    Label {
                        objectName: "styleNameProblem"
                        visible: manager.nameProblem !== ""
                        text: manager.nameProblem
                        color: Theme.danger
                        wrapMode: Text.Wrap
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }
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
                            z: 2
                            visible: manager.isMixed("fontname") && fontField.editText.length === 0
                            x: fontField.contentItem.x + fontField.contentItem.leftPadding
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Multiple values")
                            font: fontField.font
                            color: fontField.palette.placeholderText
                        }
                        function browsedFont() {
                            return popup.visible && highlightedIndex >= 0 && model && highlightedIndex < model.length
                                   ? model[highlightedIndex] : ""
                        }
                        Connections {
                            target: fontField.popup
                            function onVisibleChanged() { manager.updatePreview() }
                        }
                        Connections {
                            target: manager.catalogs
                            function onFontsChanged() { FontFaces.forget() }
                        }
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
                    FontCatalogBar {
                        id: fontCatalogBar
                        property bool made: false
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Layout.preferredWidth: 120
                        catalogs: manager.catalogs
                        fontName: fontField.editText
                        // A new model clears the ComboBox's editText.
                        onListMade: {
                            const text = fontField.editText
                            fontField.model = fonts
                            fontField.editText = text
                        }
                    }
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

    readonly property var storeShown: shownRows(true, storeRows, filterText, pendingNew && editingStore, nameField.text, heldRows(true))
    readonly property var assShown: shownRows(false, assRows, filterText, pendingNew && !editingStore, nameField.text, heldRows(false))
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
            out.push({ row: -1, name: pendingName, primary: Color.rgba(c.r / 255, c.g / 255, c.b / 255, 1).toString(),
                       outline: Color.rgba(o.r / 255, o.g / 255, o.b / 255, 1).toString(), lines: 0, pending: true })
        }
        return out
    }
    onStoreShownChanged: storeList.sync(storeShown)
    onAssShownChanged: assList.sync(assShown)
    onFilterTextChanged: dropHidden()
    onEditingChanged: if (!editing) dropHidden()
    function dropHidden() {
        // Reads the rows, not storeShown, whose binding may not have followed the filter yet.
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

    function colourClick(number, leftClick) {
        if (colourPicker.switchClicks)
            leftClick = !leftClick
        const column = colours.itemAt(number - 1)
        stylePopover.openFor(column.swatch, styleColours, () => {
            colourPicker.opened("styleEditor")
            return styleColours.begin(number)
        }, { title: column.name, dropper: !leftClick, focus: column.swatch })
    }
    QtObject {
        id: styleColours
        property int number: 1
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
        function changeColour(c) {
            const field = colours.itemAt(number - 1)
            field.setColour({ r: c.r, g: c.g, b: c.b, a: c.a })
            manager.edited(field.key)
            return true
        }
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
    FileDialog {
        id: loadDialog
        property bool toStore: false
        property Item anchor: null
        property string key
        title: qsTr("Choose ASS file")
        nameFilters: [qsTr("ASS subtitle files") + " (*.ass)"]
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
                manager.own(() => manager.styles.loadFromFile(toStore, file, names, choices))
                manager.followRows(toStore, toStore ? manager.storeSelected : manager.assSelected)
            }, key)
        }
    }
}
