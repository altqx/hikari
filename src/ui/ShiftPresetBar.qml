pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: bar
    required property ShiftTimesController shiftTimes
    required property string tool
    property var messages: null
    readonly property bool postprocessor: tool === "postprocessor"
    readonly property var names: postprocessor ? shiftTimes.postprocessorPresets : shiftTimes.shiftPresets
    readonly property string current: postprocessor ? shiftTimes.postprocessorPreset : shiftTimes.shiftPreset
    readonly property bool timingDefault: postprocessor && current !== "" && shiftTimes.timingPreset === current
    spacing: 4

    function shownName(name) {
        return bar.postprocessor && name === bar.shiftTimes.timingPreset
            ? qsTr("%1 (Timing default)").arg(name) : name
    }
    function askDelete(name) {
        if (!messages) {
            shiftTimes.deletePreset(tool, name)
            return
        }
        messages.ask({
            title: qsTr("Delete preset"),
            text: qsTr("Delete the preset “%1”?").arg(name),
            plainText: true,
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "delete")
                bar.shiftTimes.deletePreset(bar.tool, name)
        })
    }

    ComboBox {
        id: choice
        objectName: bar.tool + "Preset"
        Layout.fillWidth: true
        Accessible.name: qsTr("Preset")
        model: bar.names
        currentIndex: bar.names.indexOf(bar.current)
        displayText: qsTr("Preset: %1").arg(bar.current !== "" ? bar.shownName(bar.current) : qsTr("last used"))
        delegate: ItemDelegate {
            required property string modelData
            required property int index
            width: choice.width
            text: bar.shownName(modelData)
            highlighted: choice.highlightedIndex === index
        }
        onActivated: index => bar.shiftTimes.applyPreset(bar.tool, bar.names[index])
    }
    IconToolButton {
        id: moreButton
        objectName: bar.tool + "PresetMenuButton"
        iconRole: "panel-menu"
        text: qsTr("Preset commands")
        onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
    }
    ShellMenu {
        id: moreMenu
        objectName: bar.tool + "PresetMenu"
        ShellMenuItem {
            objectName: bar.tool + "SavePreset"
            iconRole: "save-as"
            text: qsTr("Save as preset…")
            onTriggered: namePopover.openFor("")
        }
        ShellMenuItem {
            objectName: bar.tool + "RenamePreset"
            text: qsTr("Rename preset…")
            enabled: bar.current !== ""
            onTriggered: namePopover.openFor(bar.current)
        }
        ShellMenuItem {
            objectName: bar.tool + "DeletePreset"
            iconRole: "delete"
            text: qsTr("Delete preset…")
            enabled: bar.current !== ""
            onTriggered: bar.askDelete(bar.current)
        }
        MenuSeparator { visible: bar.postprocessor; height: visible ? implicitHeight : 0 }
        ShellMenuItem {
            objectName: "postprocessorTimingDefault"
            visible: bar.postprocessor
            height: visible ? implicitHeight : 0
            checkable: true
            checked: bar.timingDefault
            enabled: bar.current !== ""
            text: qsTr("Use in the Timing layout")
            onTriggered: bar.shiftTimes.timingPreset = checked ? bar.current : ""
        }
    }
    Popover {
        id: namePopover
        objectName: bar.tool + "PresetPopover"
        title: renaming !== "" ? qsTr("Rename preset") : qsTr("Save as preset")
        showTitle: true
        initialFocus: presetName
        property string renaming: ""
        property bool saving: false
        readonly property string name: presetName.text.trim()
        readonly property bool taken: name !== renaming && bar.names.indexOf(name) >= 0
        function openFor(from) {
            renaming = from
            presetName.text = from
            presetName.selectAll()
            saving = false
            openAt(moreButton, bar)
        }
        function save() {
            if (name === "")
                return
            saving = true
            keep()
        }
        onKept: {
            if (!saving || name === "")
                return
            if (renaming === "")
                bar.shiftTimes.savePreset(bar.tool, name)
            else if (name !== renaming)
                bar.shiftTimes.renamePreset(bar.tool, renaming, name)
        }
        TextField {
            id: presetName
            objectName: bar.tool + "PresetName"
            Layout.preferredWidth: 260
            placeholderText: qsTr("Preset name")
            Accessible.name: qsTr("Preset name")
            selectByMouse: true
            onAccepted: namePopover.save()
        }
        Label {
            visible: namePopover.taken
            Layout.maximumWidth: 260
            text: qsTr("A preset named “%1” exists. Saving replaces it.").arg(namePopover.name)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.muted
        }
        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight
            PrimaryButton {
                objectName: bar.tool + "PresetSave"
                text: namePopover.taken ? qsTr("Replace") : namePopover.renaming !== "" ? qsTr("Rename") : qsTr("Save")
                enabled: namePopover.name !== ""
                onClicked: namePopover.save()
            }
            Button {
                text: qsTr("Cancel")
                onClicked: namePopover.revert()
            }
        }
    }
}
