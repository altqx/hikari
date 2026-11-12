pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: bar
    objectName: tool + "PresetBar"
    required property ShiftTimesController shiftTimes
    required property string tool
    property var messages: null
    property var appSettings: null
    readonly property bool postprocessor: tool === "postprocessor"
    readonly property var names: postprocessor ? shiftTimes.postprocessorPresets : shiftTimes.shiftPresets
    readonly property var rows: postprocessor ? shiftTimes.postprocessorPresetRows : shiftTimes.shiftPresetRows
    readonly property string current: postprocessor ? shiftTimes.postprocessorPreset : shiftTimes.shiftPreset
    readonly property string currentTier: postprocessor ? shiftTimes.postprocessorPresetTier : shiftTimes.shiftPresetTier
    readonly property bool tiered: shiftTimes.hasProject
    readonly property bool readOnly: current !== "" && rows.some(r => r.name === current && r.tier === currentTier && r.hidden)
    readonly property bool fromMine: currentTier === "mine"
    readonly property bool timingDefault: postprocessor && current !== "" && shiftTimes.timingPreset === current
    spacing: 4

    function shownName(name) {
        return bar.postprocessor && name === bar.shiftTimes.timingPreset
            ? qsTr("%1 (Timing default)").arg(name) : name
    }
    function namesIn(tier) {
        return rows.filter(r => r.tier === tier).map(r => r.name)
    }
    function transfer(move) {
        const name = current
        const from = currentTier
        const run = choices => move ? bar.shiftTimes.movePreset(bar.tool, name, from, choices)
                                    : bar.shiftTimes.copyPreset(bar.tool, name, from, choices)
        const clashes = shiftTimes.presetClashes(tool, name, from)
        if (clashes.length === 0) {
            run([])
            return
        }
        clashReviewLoader.active = true
        clashReviewLoader.item.review({
            title: move ? qsTr("Move preset") : qsTr("Copy preset"),
            text: from === "mine" ? qsTr("The project already has a preset named “%1”.").arg(name)
                                  : qsTr("You already have a preset named “%1”.").arg(name),
            rows: clashes.map(c => ({ name: c.name, difference: "", suggestion: c.suggestion })),
            acceptText: count => move ? qsTr("Move") : qsTr("Copy"),
            nameTaken: n => bar.rows.some(r => r.name === n && r.tier !== from)
        }, results => {
            if (results)
                run(results)
        })
    }
    function askDelete(name) {
        if (!messages) {
            shiftTimes.deletePreset(tool, name, currentTier)
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
                bar.shiftTimes.deletePreset(bar.tool, name, bar.currentTier)
        })
    }

    ComboBox {
        id: choice
        objectName: bar.tool + "Preset"
        Layout.fillWidth: true
        Accessible.name: qsTr("Preset")
        model: bar.rows
        textRole: "name"
        currentIndex: bar.rows.findIndex(r => r.name === bar.current && r.tier === bar.currentTier)
        displayText: qsTr("Preset: %1").arg(bar.current !== "" ? bar.shownName(bar.current) : qsTr("last used"))
        delegate: ItemDelegate {
            id: presetRow
            required property var modelData
            required property int index
            readonly property bool heading: bar.tiered && (index === 0 || bar.rows[index - 1].tier !== modelData.tier)
            objectName: bar.tool + "PresetRow_" + modelData.tier + "_" + modelData.name
            width: choice.width
            highlighted: choice.highlightedIndex === index
            topPadding: heading ? 26 : 4
            Accessible.name: modelData.hidden ? qsTr("%1, hidden by the project’s copy").arg(bar.shownName(modelData.name))
                                              : bar.shownName(modelData.name)
            ToolTip.visible: hovered && modelData.hidden
            ToolTip.text: qsTr("Hidden by the project’s copy")
            Label {
                visible: presetRow.heading
                x: 10
                y: 6
                text: presetRow.modelData.tier === "project" ? qsTr("This project") : qsTr("Mine")
                font.bold: true
                font.pixelSize: 11
                color: Theme.muted
            }
            contentItem: RowLayout {
                spacing: 8
                Label {
                    text: bar.shownName(presetRow.modelData.name)
                    color: presetRow.modelData.hidden ? Theme.muted : Theme.text
                    font.italic: presetRow.modelData.hidden
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                TierChip {
                    visible: bar.tiered
                    tier: presetRow.modelData.tier
                    enabled: !presetRow.modelData.hidden
                }
            }
        }
        onActivated: index => bar.shiftTimes.applyPreset(bar.tool, bar.rows[index].name, bar.rows[index].tier)
    }
    TierChip {
        objectName: bar.tool + "PresetTier"
        visible: bar.tiered && bar.current !== ""
        tier: bar.currentTier
        enabled: !bar.readOnly
        ToolTip.visible: bar.readOnly && tierHover.hovered
        ToolTip.text: qsTr("Hidden by the project’s copy")
        HoverHandler { id: tierHover }
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
            iconRole: "edit"
            text: qsTr("Rename preset…")
            enabled: bar.current !== "" && !bar.readOnly
            onTriggered: namePopover.openFor(bar.current)
        }
        ShellMenuItem {
            objectName: bar.tool + "DeletePreset"
            iconRole: "delete"
            text: qsTr("Delete preset…")
            enabled: bar.current !== "" && !bar.readOnly
            onTriggered: bar.askDelete(bar.current)
        }
        MenuSeparator { visible: bar.tiered; height: visible ? implicitHeight : 0 }
        ShellMenuItem {
            objectName: bar.tool + "CopyPresetToTier"
            visible: bar.tiered
            height: visible ? implicitHeight : 0
            iconRole: "copy-to-tier"
            text: bar.fromMine ? qsTr("Copy to project") : qsTr("Copy to mine")
            enabled: bar.current !== ""
            onTriggered: bar.transfer(false)
        }
        ShellMenuItem {
            objectName: bar.tool + "MovePresetToTier"
            visible: bar.tiered
            height: visible ? implicitHeight : 0
            iconRole: "move-to-tier"
            text: bar.fromMine ? qsTr("Move to project") : qsTr("Move to mine")
            enabled: bar.current !== ""
            onTriggered: bar.transfer(true)
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
        property string saveTier: ""
        readonly property string name: presetName.text.trim()
        readonly property string targetTier: renaming !== "" ? bar.currentTier : saveButton.tier
        readonly property bool taken: name !== renaming && bar.namesIn(targetTier).indexOf(name) >= 0
        function openFor(from) {
            renaming = from
            presetName.text = from
            presetName.selectAll()
            saving = false
            openAt(moreButton, bar)
        }
        function save() { saveTo(renaming !== "" ? "" : saveButton.tier) }
        function saveTo(tier) {
            if (name === "")
                return
            saveTier = tier
            saving = true
            keep()
        }
        onKept: {
            if (!saving || name === "")
                return
            if (renaming === "")
                bar.shiftTimes.savePreset(bar.tool, name, saveTier)
            else if (name !== renaming)
                bar.shiftTimes.renamePreset(bar.tool, renaming, name, bar.currentTier)
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
            TierSaveButton {
                id: saveButton
                objectName: bar.tool + "PresetSaveTier"
                faceName: bar.tool + "PresetSave"
                visible: namePopover.renaming === ""
                listId: bar.postprocessor ? "postprocessorPresets" : "shiftPresets"
                settings: bar.appSettings
                hasProject: bar.tiered
                plainText: namePopover.taken ? qsTr("Replace") : qsTr("Save")
                faceEnabled: namePopover.name !== ""
                onSave: tier => namePopover.saveTo(tier)
            }
            PrimaryButton {
                objectName: bar.tool + "PresetRename"
                visible: namePopover.renaming !== ""
                text: namePopover.taken ? qsTr("Replace") : qsTr("Rename")
                enabled: namePopover.name !== ""
                onClicked: namePopover.save()
            }
            Button {
                text: qsTr("Cancel")
                onClicked: namePopover.revert()
            }
        }
    }
    Loader {
        id: clashReviewLoader
        active: false
        sourceComponent: ClashReview {
            objectName: bar.tool + "PresetClashReview"
            dialogId: "presetClashes"
            settings: bar.appSettings
        }
    }
}
