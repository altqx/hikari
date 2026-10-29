import QtQuick
import QtQuick.Controls
import Hikari.Ui

QtObject {
    id: entries

    required property Menu menu
    required property QtObject after
    property var macros: []
    property bool enabled: true
    signal triggered(var macro)

    property var shown: []

    readonly property Component itemComponent: Component {
        ShellMenuItem {
            required property var macro
            objectName: "macro_" + macro.name
            enabled: entries.enabled
            onTriggered: entries.triggered(macro)
        }
    }
    readonly property Component submenuComponent: Component {
        ShellMenu {}
    }

    function tree(list) {
        const top = []
        for (const macro of list) {
            const pieces = macro.name.split("/")
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
                const submenu = submenuComponent.createObject(target, { title: entry.title, objectName: "macroMenu_" + entry.path })
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
