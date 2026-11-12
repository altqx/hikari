pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// Save to project or Save to mine, remembered per list in workspace.saveTiers. Without a Project
// it is a plain button that saves to Mine.
Row {
    id: split
    required property string listId
    required property var settings
    property bool hasProject: false
    property string plainText: qsTr("Save")
    property string faceName: objectName + "Face"
    property alias faceEnabled: face.enabled
    readonly property string tier: hasProject ? remembered : "mine"
    property string remembered: readTier()
    signal save(string tier)
    spacing: 1

    function readTier() {
        const rows = split.settings ? split.settings.value("workspace.saveTiers") || [] : []
        const row = rows.find(r => r.startsWith(split.listId + "\t"))
        return row && row.endsWith("\tmine") ? "mine" : "project"
    }
    function choose(tier) {
        const rows = (split.settings.value("workspace.saveTiers") || []).filter(r => !r.startsWith(split.listId + "\t"))
        rows.push(split.listId + "\t" + tier)
        split.settings.setValue("workspace.saveTiers", rows)
        remembered = tier
    }

    PrimaryButton {
        id: face
        objectName: split.faceName
        text: !split.hasProject ? split.plainText : split.tier === "project" ? qsTr("Save to project") : qsTr("Save to mine")
        onClicked: split.save(split.tier)
    }
    PrimaryButton {
        id: arrow
        objectName: split.objectName + "Arrow"
        visible: split.hasProject
        enabled: face.enabled
        implicitWidth: 26
        leftPadding: 0
        rightPadding: 0
        text: qsTr("Where to save")
        Accessible.role: Accessible.ButtonMenu
        ToolTip.text: text
        ToolTip.visible: hovered
        contentItem: Icon {
            iconRole: "chevron-down"
            color: Theme.onAccent
        }
        onClicked: tierMenu.popup(arrow, 0, arrow.height)
    }
    ShellMenu {
        id: tierMenu
        objectName: split.objectName + "Menu"
        ShellMenuItem {
            objectName: split.objectName + "Project"
            iconRole: "project"
            text: qsTr("Save to project")
            checkable: true
            keepsMenuOpen: false
            checked: split.tier === "project"
            onTriggered: split.choose("project")
        }
        ShellMenuItem {
            objectName: split.objectName + "Mine"
            iconRole: "mine"
            text: qsTr("Save to mine")
            checkable: true
            keepsMenuOpen: false
            checked: split.tier === "mine"
            onTriggered: split.choose("mine")
        }
    }
}
