pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: row
    // The Settings window; with it and a setting, the row shows Changed, Reset and its ⋯ menu.
    property var host: null
    // It sits on the Project tab.
    property bool project: false
    property string setting
    property var settings: setting.length > 0 ? [setting] : []
    property string iconRole
    property string text: setting.length > 0 ? SettingsCatalogue.label(setting) : ""
    property string note: setting.length > 0 ? SettingsCatalogue.description(setting) : ""
    property int labelWidth: 220
    // The controls sit in the label's place, as a check box does.
    property bool inline: false
    default property alias controls: controlRow.data
    readonly property bool tracked: host !== null && setting.length > 0
    readonly property bool changed: tracked && host.isChanged(settings)
    readonly property var elsewhere: tracked ? host.elsewhere(setting) : null
    readonly property bool readOnly: elsewhere !== null && elsewhere.readOnly === true
    readonly property bool flashing: tracked && host.flashSetting === setting

    objectName: setting.length > 0 ? "settingRow_" + setting : ""
    visible: !tracked || host.shows(setting)
    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: if (tracked) host.registerRow(setting, row, project)

    HoverHandler { id: hover }

    Rectangle {
        x: -8
        y: -4
        width: row.width + 16
        height: row.height + 8
        radius: 2
        color: row.flashing ? Theme.select : hover.hovered ? Theme.raised : "transparent"
        border.width: row.flashing && Theme.highContrast ? 1 : 0
        border.color: Theme.accent
        Behavior on color { enabled: !row.flashing; ColorAnimation { duration: 2000 } }
    }
    ColumnLayout {
    id: column
    width: row.width
    spacing: 2
    RowLayout {
        id: header
        spacing: 6
        Layout.fillWidth: true
        Label {
            objectName: "settingLabel"
            text: row.text
            visible: !row.inline && text.length > 0
            wrapMode: Text.Wrap
            color: Theme.text
            Layout.maximumWidth: implicitWidth
            Layout.fillWidth: true
        }
        RowLayout {
            id: inlineSlot
            visible: row.inline
            spacing: 8
            Layout.fillWidth: false
        }
        Label {
            objectName: "settingChanged"
            visible: row.changed
            text: qsTr("Changed")
            color: Theme.muted
        }
        IconToolButton {
            objectName: "settingReset"
            visible: row.changed && !row.readOnly
            iconRole: "restore-default"
            text: qsTr("Reset %1").arg(row.text)
            tip: row.host && row.host.projectTab ? qsTr("Use yours") : qsTr("Reset to the default")
            onClicked: row.host.resetSetting(row.settings)
        }
        Item { Layout.fillWidth: true }
        IconToolButton {
            id: more
            objectName: "settingMore"
            visible: row.tracked && (hover.hovered || more.activeFocus || menu.visible || row.focusWithin)
            iconRole: "menu-more"
            text: qsTr("%1 options").arg(row.text)
            onClicked: menu.popup(more, 0, more.height)
        }
    }
    Label {
        visible: row.note.length > 0
        text: row.note
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    RowLayout {
        visible: row.elsewhere !== null
        spacing: 4
        Layout.fillWidth: true
        Icon {
            visible: row.elsewhere !== null && row.elsewhere.warning === true
            iconRole: "warning"
        }
        Label {
            objectName: "settingElsewhere"
            text: row.elsewhere ? row.elsewhere.text : ""
            color: Theme.muted
            font.pixelSize: 12
            font.underline: row.elsewhere !== null && row.elsewhere.link !== undefined
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.role: row.elsewhere && row.elsewhere.link !== undefined ? Accessible.Link : Accessible.StaticText
            Accessible.name: text
            TapHandler {
                enabled: row.elsewhere !== null && row.elsewhere.link !== undefined
                cursorShape: Qt.PointingHandCursor
                onTapped: row.host.follow(row.elsewhere.link, row.setting)
            }
        }
    }
    RowLayout {
        id: below
        visible: !row.inline
        spacing: 8
        Layout.topMargin: 4
        Layout.fillWidth: true
    }
    }
    RowLayout {
        id: controlRow
        parent: row.inline ? inlineSlot : below
        enabled: !row.readOnly
        spacing: 8
        Layout.fillWidth: true
    }
    readonly property bool focusWithin: {
        for (let item = Window.activeFocusItem; item; item = item.parent)
            if (item === row)
                return true
        return false
    }
    ShellMenu {
        id: menu
        ShellMenuItem {
            objectName: "settingMenuReset"
            text: qsTr("Reset")
            enabled: row.changed && !row.readOnly
            onTriggered: row.host.resetSetting(row.settings)
        }
        ShellMenuItem {
            objectName: "settingMenuCopyId"
            text: qsTr("Copy setting id")
            onTriggered: row.host.copyText(row.setting)
        }
        ShellMenuItem {
            objectName: "settingMenuCopyLink"
            text: qsTr("Copy link")
            onTriggered: row.host.copyText(row.host.linkTo(row.setting))
        }
        ShellMenuItem {
            objectName: "settingMenuOpenFile"
            text: qsTr("Open settings file")
            onTriggered: row.host.openSettingsFile(row.setting)
        }
    }
}
