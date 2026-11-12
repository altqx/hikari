pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// Save to project or Save to mine, remembered per list in workspace.saveTiers; Mine without a Project.
ShellMenu {
    id: menu
    required property string listId
    required property var settings
    property bool hasProject: false
    property string itemPrefix: objectName
    readonly property string tier: hasProject ? remembered : "mine"
    readonly property string saveText: tier === "project" ? qsTr("Save to project") : qsTr("Save to mine")
    property string remembered: readTier()

    function readTier() {
        const rows = menu.settings ? menu.settings.value("workspace.saveTiers") || [] : []
        const row = rows.find(r => r.startsWith(menu.listId + "\t"))
        return row && row.endsWith("\tmine") ? "mine" : "project"
    }
    function choose(tier) {
        remembered = tier
        if (!menu.settings)
            return
        const rows = (menu.settings.value("workspace.saveTiers") || []).filter(r => !r.startsWith(menu.listId + "\t"))
        rows.push(menu.listId + "\t" + tier)
        menu.settings.setValue("workspace.saveTiers", rows)
    }

    ShellMenuItem {
        objectName: menu.itemPrefix + "Project"
        iconRole: "project"
        text: qsTr("Save to project")
        checkable: true
        keepsMenuOpen: false
        checked: menu.tier === "project"
        onTriggered: menu.choose("project")
    }
    ShellMenuItem {
        objectName: menu.itemPrefix + "Mine"
        iconRole: "mine"
        text: qsTr("Save to mine")
        checkable: true
        keepsMenuOpen: false
        checked: menu.tier === "mine"
        onTriggered: menu.choose("mine")
    }
}
