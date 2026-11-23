import QtQuick
import QtQuick.Controls
import Hikari.Ui

QtObject {
    id: entries

    required property Menu menu
    required property QtObject after
    property var macros: []
    property bool enabled: true
    property var keys: null
    // Why a macro can't run now, or "".
    property var reasonOf: null
    // A macro's check: {reason, note, help, active}, each optional; `active` gives the row a check mark.
    property var checkOf: null
    // Entries name their place with `titles` (a list), else with `name` split at "/".
    property string itemPrefix: "macro_"
    property string menuPrefix: "macroMenu_"
    signal triggered(var macro)

    property var shown: []

    readonly property Component itemComponent: Component {
        ShellMenuItem {
            id: macroItem
            required property var macro
            readonly property var check: entries.checkOf ? entries.checkOf(macro) : ({})
            objectName: entries.itemPrefix + (macro.id ?? macro.name)
            reason: entries.reasonOf ? entries.reasonOf(macro) : (check.reason ?? "")
            description: check.note ?? ""
            help: check.help ?? ""
            checkable: check.active !== undefined
            checked: check.active === true
            keepsMenuOpen: false
            enabled: entries.enabled && reason.length === 0
            shortcutText: entries.keys ? entries.keys(macro) : ""
            onTriggered: {
                checked = Qt.binding(() => macroItem.check.active === true)
                entries.triggered(macro)
            }
        }
    }
    readonly property Component submenuComponent: Component {
        ShellMenu {}
    }

    function tree(list) {
        const top = []
        for (const macro of list) {
            const pieces = macro.titles !== undefined ? macro.titles : macro.name.split("/")
            let level = top
            let path = ""
            for (let i = 0; i < pieces.length - 1; ++i) {
                path += (i > 0 ? "/" : "") + pieces[i]
                let submenu = level.find(entry => entry.children !== undefined && entry.title === pieces[i])
                if (!submenu) {
                    submenu = { title: pieces[i], path: path, children: [] }
                    level.push(submenu)
                }
                level = submenu.children
            }
            level.push({ label: pieces[pieces.length - 1], macro: macro })
        }
        return top
    }

    function fill(target, at, level) {
        const made = []
        for (const entry of level) {
            if (entry.children !== undefined) {
                const submenu = submenuComponent.createObject(target, { title: entry.title, objectName: entries.menuPrefix + entry.path })
                fill(submenu, 0, entry.children)
                target.insertMenu(at++, submenu)
                made.push({ menu: submenu })
            } else {
                const item = itemComponent.createObject(target.contentItem, { macro: entry.macro, text: entry.label })
                target.insertItem(at++, item)
                made.push({ item: item })
            }
        }
        return made
    }

    function rebuild() {
        for (const entry of shown) {
            if (entry.menu)
                menu.removeMenu(entry.menu)
            else
                menu.removeItem(entry.item)
        }
        let at = 0
        while (at < menu.count && menu.itemAt(at) !== after)
            ++at
        shown = fill(menu, at + 1, tree(macros))
    }

    onMacrosChanged: rebuild()
    Component.onCompleted: rebuild()
}
