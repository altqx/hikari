pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: window
    required property AutomationDialogController controller
    required property ColourPickerController colourPicker
    property var messages: null

    title: controller.title
    dialogId: controller.dialogId
    guideId: "automation"
    resizable: true
    modal: true

    property bool altHeld: false

    function values() { return controlGrid.values() }
    function press(index) { controller.finish(index, values()) }

    function fill(list) { controlGrid.fill(list) }
    function presetKey(row) {
        return controller.hasProject ? row.tier + "_" + row.name : row.name
    }
    function transferPreset(name, from, move) {
        const run = choices => move ? controller.movePreset(name, from, choices) : controller.copyPreset(name, from, choices)
        const clashes = controller.presetClashes(name, from)
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
            nameTaken: n => window.controller.presetRows.some(r => r.name === n && r.tier !== from)
        }, results => {
            if (results && window.controller.open)
                run(results)
        })
    }
    function placeTierMenus() {
        const tierMenus = [copyToProjectMenu, copyToMineMenu, moveToProjectMenu, moveToMineMenu]
        for (let i = presetsMenu.count - 1; i >= 0; --i)
            if (presetsMenu.itemAt(i) && tierMenus.indexOf(presetsMenu.itemAt(i).subMenu) >= 0)
                presetsMenu.takeMenu(i)
        if (!controller.hasProject)
            return
        for (let i = 0; i < presetsMenu.count; ++i)
            if (presetsMenu.itemAt(i) === tierSeparator) {
                tierMenus.forEach((m, k) => presetsMenu.insertMenu(i + k, m))
                return
            }
    }
    function askDelete(name) { askDeleteIn(name, "") }
    function askDeleteIn(name, tier) {
        if (!messages) {
            controller.deletePreset(name, tier)
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
            if (id === "delete" && controller.open)
                controller.deletePreset(name, tier)
        })
    }
    function pressEnterButton() {
        const b = madeButtons[controller.enterButton]
        if (b)
            b.click()
    }


    onActiveFocusItemChanged: {
        // Qt: the DialogButtonBox is a focus scope but no control, so on activation it can keep its button's focus.
        if (activeFocusItem && (activeFocusItem === buttonBox || activeFocusItem === buttonBox.contentItem)
                && defaultButton && defaultButton.Window.window === window) {
            defaultButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        const cell = controlGrid.cellOf(activeFocusItem)
        if (cell)
            controlGrid.focusedCell = cell
        else if (activeFocusItem && madeButtons.indexOf(activeFocusItem) >= 0)
            controlGrid.focusedCell = null
        if (cell && cell.item)
            controlGrid.reveal(activeFocusItem)
    }
    onActiveChanged: if (!active) altHeld = false
    function trackAlt(event, down) {
        if (event.key === Qt.Key_Alt)
            altHeld = down
        event.accepted = false
    }

    function show() {
        makeButtons()
        // A ListView places new items on its next layout; force it so the Enter button exists when open() focuses it.
        if (buttonBox.contentItem && typeof buttonBox.contentItem.forceLayout === "function")
            buttonBox.contentItem.forceLayout()
        // Qt 6.11: the box keeps its list at an earlier width when its own width is unchanged; re-aligning refits it.
        buttonBox.alignment = 0
        buttonBox.alignment = Qt.AlignRight
        if (visible) {
            raise()
            if (defaultButton)
                defaultButton.forceActiveFocus(Qt.TabFocusReason)
        } else {
            open()
        }
    }
    Connections {
        target: window.controller
        function onChanged() {
            if (window.controller.open)
                Qt.callLater(window.show)
            else if (window.visible)
                window.close()
        }
    }
    onRejected: if (controller.open) controller.finish(-1, values())

    leadingActions: Button {
        id: moreButton
        objectName: "automationDialogMore"
        visible: window.controller.remembers
        text: qsTranslate("HikariSub.Workspace", "More options")
        display: AbstractButton.IconOnly
        implicitWidth: implicitHeight
        down: pressed || moreMenu.visible
        contentItem: Item {
            implicitWidth: 16
            implicitHeight: 16
            Icon {
                anchors.centerIn: parent
                iconRole: "panel-menu"
            }
        }
        Accessible.name: text
        ToolTip.visible: hovered && !moreMenu.visible
        ToolTip.text: text
        onClicked: moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight - 4)
        Keys.onPressed: event => window.trackAlt(event, true)
        Keys.onReleased: event => window.trackAlt(event, false)
    }
    ShellMenu {
        id: moreMenu
        objectName: "automationDialogMenu"
        ShellMenuItem {
            objectName: "dialogLastUsed"
            iconRole: "dialog-memory"
            text: qsTr("Last used")
            enabled: window.controller.hasLastUsed
            onTriggered: window.fill(window.controller.lastUsedValues())
        }
        ShellMenu {
            id: presetsMenu
            objectName: "dialogPresetsMenu"
            title: qsTr("Presets")
            iconRole: "dialog-memory"
            onAboutToShow: window.placeTierMenus()
            Instantiator {
                model: window.controller.presetRows
                delegate: ShellMenuItem {
                    required property var modelData
                    objectName: "dialogPreset_" + window.presetKey(modelData)
                    text: modelData.name
                    iconRole: window.controller.hasProject ? modelData.tier : ""
                    detailText: !window.controller.hasProject ? "" : modelData.tier === "project" ? qsTr("Project") : qsTr("Mine")
                    enabled: !modelData.hidden
                    help: modelData.hidden ? qsTr("Hidden by the project’s copy") : ""
                    onTriggered: window.fill(window.controller.presetValues(modelData.name, modelData.tier))
                }
                onObjectAdded: (index, object) => presetsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => presetsMenu.removeItem(object)
            }
            MenuSeparator {
                visible: window.controller.presetRows.length > 0
                height: window.controller.presetRows.length > 0 ? implicitHeight : 0
            }
            ShellMenuItem {
                objectName: "dialogSavePreset"
                iconRole: "save-as"
                text: qsTr("Save as preset…")
                onTriggered: savePopover.openFor()
            }
            MenuSeparator {}
            MenuSeparator {
                id: tierSeparator
                objectName: "dialogTierSeparator"
                visible: window.controller.hasProject
                height: window.controller.hasProject ? implicitHeight : 0
            }
            ShellMenu {
                id: deleteMenu
                objectName: "dialogDeletePresetMenu"
                title: qsTr("Delete preset")
                iconRole: "delete"
                enabled: window.controller.presets.length > 0
                Instantiator {
                    model: window.controller.presetRows.filter(r => !r.hidden)
                    delegate: ShellMenuItem {
                        required property var modelData
                        objectName: "dialogDeletePreset_" + window.presetKey(modelData)
                        text: modelData.name
                        detailText: !window.controller.hasProject ? "" : modelData.tier === "project" ? qsTr("Project") : qsTr("Mine")
                        onTriggered: window.askDeleteIn(modelData.name, modelData.tier)
                    }
                    onObjectAdded: (index, object) => deleteMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => deleteMenu.removeItem(object)
                }
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "dialogReset"
            iconRole: "restore-default"
            text: qsTr("Reset to the script’s values")
            onTriggered: window.fill(window.controller.scriptValues())
        }
    }
    Popover {
        id: savePopover
        objectName: "savePresetPopover"
        title: qsTr("Save as preset")
        showTitle: true
        initialFocus: presetName
        property bool saving: false
        property string saveTier: ""
        readonly property string name: presetName.text.trim()
        readonly property bool taken: window.controller.presetRows.some(r => r.name === name && r.tier === presetSaveTier.tier)
        function openFor() {
            presetName.text = ""
            saving = false
            openAt(moreButton, window.contentItem)
        }
        function save() { saveTo(presetSaveTier.tier) }
        function saveTo(tier) {
            if (name === "")
                return
            saveTier = tier
            saving = true
            keep()
        }
        onKept: if (saving && name !== "") window.controller.savePreset(name, window.values(), saveTier)
        TextField {
            id: presetName
            objectName: "presetName"
            Layout.preferredWidth: 260
            placeholderText: qsTr("Preset name")
            Accessible.name: qsTr("Preset name")
            selectByMouse: true
            onAccepted: savePopover.save()
        }
        Label {
            objectName: "presetTaken"
            visible: savePopover.taken
            Layout.maximumWidth: 260
            text: qsTr("A preset named “%1” exists. Saving replaces it.").arg(savePopover.name)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.muted
        }
        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight
            TierSaveButton {
                id: presetSaveTier
                objectName: "presetSaveTier"
                faceName: "presetSave"
                listId: "dialogPresets"
                settings: window.settings
                hasProject: window.controller.hasProject
                plainText: savePopover.taken ? qsTr("Replace") : qsTr("Save")
                faceEnabled: savePopover.name !== ""
                onSave: tier => savePopover.saveTo(tier)
            }
            Button {
                objectName: "presetCancel"
                text: qsTr("Cancel")
                onClicked: savePopover.revert()
            }
        }
    }

    TierPresetMenu { id: copyToProjectMenu; name: "dialogCopyToProjectMenu"; title: qsTr("Copy to project"); from: "mine"; move: false; host: window }
    TierPresetMenu { id: copyToMineMenu; name: "dialogCopyToMineMenu"; title: qsTr("Copy to mine"); from: "project"; move: false; host: window }
    TierPresetMenu { id: moveToProjectMenu; name: "dialogMoveToProjectMenu"; title: qsTr("Move to project"); from: "mine"; move: true; host: window }
    TierPresetMenu { id: moveToMineMenu; name: "dialogMoveToMineMenu"; title: qsTr("Move to mine"); from: "project"; move: true; host: window }
    component TierPresetMenu: ShellMenu {
        id: tierMenu
        required property string name
        required property string from
        required property bool move
        required property var host
        objectName: name
        iconRole: move ? "move-to-tier" : "copy-to-tier"
        enabled: tierMenu.host.controller.presetRows.some(r => r.tier === tierMenu.from)
        Instantiator {
            model: tierMenu.host.controller.presetRows.filter(r => r.tier === tierMenu.from)
            delegate: ShellMenuItem {
                required property var modelData
                objectName: tierMenu.name + "_" + modelData.name
                text: modelData.name
                onTriggered: tierMenu.host.transferPreset(modelData.name, tierMenu.from, tierMenu.move)
            }
            onObjectAdded: (index, object) => tierMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => tierMenu.removeItem(object)
        }
    }
    Loader {
        id: clashReviewLoader
        active: false
        sourceComponent: ClashReview {
            objectName: "dialogPresetClashReview"
            dialogId: "presetClashes"
            settings: window.settings
        }
    }

    // One role for every button, so DialogButtonBox keeps the script's order.
    property var madeButtons: []
    function makeButtons() {
        for (const b of madeButtons) {
            buttonBox.removeItem(b)
            b.destroy()
        }
        const list = []
        controller.buttons.forEach((label, i) => {
            const kind = i === controller.enterButton ? primaryButton : plainButton
            const b = kind.createObject(null, { label: label, index: i, objectName: "dialogButton" + i })
            buttonBox.addItem(b)
            list.push(b)
        })
        madeButtons = list
        defaultButton = list[controller.enterButton] || null
    }
    Component {
        id: primaryButton
        PrimaryButton {
            id: b
            required property string label
            required property int index
            text: controlGrid.plain(label)
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            contentItem: Label {
                text: controlGrid.shownLabel(b.label)
                textFormat: Text.StyledText
                color: b.enabled ? Theme.onAccent : Theme.disabled
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: window.press(index)
            Keys.onPressed: event => window.trackAlt(event, true)
            Keys.onReleased: event => window.trackAlt(event, false)
        }
    }
    Component {
        id: plainButton
        Button {
            id: b
            required property string label
            required property int index
            text: controlGrid.plain(label)
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            contentItem: Label {
                text: controlGrid.shownLabel(b.label)
                textFormat: Text.StyledText
                color: b.enabled ? b.palette.buttonText : Theme.disabled
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: window.press(index)
            Keys.onPressed: event => window.trackAlt(event, true)
            Keys.onReleased: event => window.trackAlt(event, false)
        }
    }

    Item {
        id: keys
        objectName: "automationDialogContent"
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitWidth: controlGrid.implicitWidth
        implicitHeight: controlGrid.implicitHeight + (noticeBar.visible ? noticeBar.implicitHeight + 12 : 0)
        Keys.onPressed: event => window.trackAlt(event, true)
        Keys.onReleased: event => window.trackAlt(event, false)

        Repeater {
            model: window.controller.buttons
            delegate: Item {
                id: key
                required property string modelData
                required property int index
                Shortcut {
                    sequence: controlGrid.mnemonic(key.modelData) !== "" ? "Alt+" + controlGrid.mnemonic(key.modelData) : ""
                    enabled: sequence !== "" && window.visible
                    onActivated: {
                        const b = window.madeButtons[key.index]
                        if (b)
                            b.click()
                    }
                }
            }
        }

        InlineBar {
            id: noticeBar
            objectName: "automationDialogNotice"
            visible: window.controller.notice !== ""
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            text: window.controller.notice
            closable: false
        }

        ScriptDialogControls {
            id: controlGrid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: noticeBar.visible ? noticeBar.bottom : parent.top
            anchors.topMargin: noticeBar.visible ? 12 : 0
            anchors.bottom: parent.bottom
            controls: window.controller.controls
            columns: window.controller.columns
            altHeld: window.altHeld
            shortcutsEnabled: window.visible
            colourPicker: window.colourPicker
            host: window.controller
            onEnterPressed: window.pressEnterButton()
        }
    }
}
