import QtQuick
import QtQuick.Controls
import Hikari.Ui

QtObject {
    id: entries

    required property Menu menu
    // The entries follow this item; without one they end the menu.
    property QtObject after: null
    // PluginRuntime.menuItems rows of one location, in order.
    property var items: []
    property var runtime: null
    property int shellWindow: 0
    property var keys: null
    signal triggered(var item)

    property var shown: []

    readonly property Component itemComponent: Component {
        ShellMenuItem {
            required property var entry
            objectName: entry.name
            text: entry.title
            iconRole: entry.icon
            help: entry.pluginName
            shortcutText: entries.keys ? entries.keys(entry) : ""
            reason: {
                const runtime = entries.runtime
                if (!runtime)
                    return ""
                runtime.conditionsRevision
                return runtime.disabledReason(entry.id, entries.shellWindow)
            }
            enabled: reason.length === 0
            keepsMenuOpen: false
            onTriggered: entries.triggered(entry)
        }
    }
    readonly property Component separatorComponent: Component {
        MenuSeparator {}
    }

    function rebuild() {
        for (const made of shown)
            menu.removeItem(made)
        let at = menu.count
        if (after) {
            at = 0
            while (at < menu.count && menu.itemAt(at) !== after)
                ++at
            ++at
        }
        const made = []
        let previous = null
        for (const entry of items) {
            if (!previous || previous.plugin !== entry.plugin || previous.group !== entry.group) {
                const separator = separatorComponent.createObject(menu.contentItem)
                menu.insertItem(at++, separator)
                made.push(separator)
            }
            const item = itemComponent.createObject(menu.contentItem, { entry: entry })
            menu.insertItem(at++, item)
            made.push(item)
            previous = entry
        }
        shown = made
    }

    onItemsChanged: rebuild()
    Component.onCompleted: rebuild()
}
