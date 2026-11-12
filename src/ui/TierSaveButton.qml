pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A TierSaveMenu split button; without a Project, a plain button that saves to Mine.
Row {
    id: split
    required property string listId
    required property var settings
    property bool hasProject: false
    property string plainText: qsTr("Save")
    property string faceName: objectName + "Face"
    property bool faceEnabled: true
    // Set for the icon-only form of a tool strip; plainText is then its hover name.
    property string iconRole
    readonly property bool iconOnly: iconRole.length > 0
    readonly property string faceText: !hasProject ? plainText
                                     : tier === "project" ? qsTr("%1 in the project").arg(plainText)
                                     : qsTr("%1 in mine").arg(plainText)
    readonly property string tier: tierMenu.tier
    signal save(string tier)
    spacing: 1

    PrimaryButton {
        id: textFace
        objectName: split.iconOnly ? "" : split.faceName
        visible: !split.iconOnly
        enabled: split.faceEnabled
        text: !split.hasProject ? split.plainText : tierMenu.saveText
        onClicked: split.save(split.tier)
    }
    IconToolButton {
        id: iconFace
        objectName: split.iconOnly ? split.faceName : ""
        visible: split.iconOnly
        enabled: split.faceEnabled
        iconRole: split.iconRole
        text: split.faceText
        onClicked: split.save(split.tier)
    }
    IconToolButton {
        id: iconArrow
        objectName: split.iconOnly ? split.objectName + "Arrow" : ""
        visible: split.iconOnly && split.hasProject
        enabled: split.faceEnabled
        iconRole: "chevron-down"
        iconSize: 12
        implicitWidth: 18
        text: qsTr("Where to save")
        Accessible.role: Accessible.ButtonMenu
        onClicked: tierMenu.popup(iconArrow, 0, iconArrow.height)
    }
    PrimaryButton {
        id: arrow
        objectName: split.iconOnly ? "" : split.objectName + "Arrow"
        visible: !split.iconOnly && split.hasProject
        enabled: split.faceEnabled
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
    TierSaveMenu {
        id: tierMenu
        objectName: split.objectName + "Menu"
        itemPrefix: split.objectName
        listId: split.listId
        settings: split.settings
        hasProject: split.hasProject
    }
}
