pragma Translator: "HikariSub.VisualTools"
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "tagsEditionDialog"
    property AllTagsEditor editor
    title: qsTr("Edit tags")
    dialogId: "allTagsEdition"
    resizable: true
    initialFocus: tagList

    readonly property var changeNames: [qsTr("Set"), qsTr("Add"), qsTr("Multiply"), qsTr("Gradient")]
    readonly property var placingNames: [qsTr("At the cursor"), qsTr("At the start of the text"),
                                         qsTr("Clip rectangle with %1 animation").arg("\\t")]
    readonly property var multiplyNames: [qsTr("Set by line number"), qsTr("Add by line number")]
    readonly property var gradientNames: [qsTr("Across characters, increasing"), qsTr("Across characters, decreasing"),
                                          qsTr("Across lines, increasing"), qsTr("Across lines, decreasing")]
    property bool advancedOpen: false
    onVisibleChanged: if (visible) advancedOpen = false
    // MUST KILL: changeOption is a magic int (eight paste modes) remapped by hand into change and variant
    readonly property int changeOption: editor ? editor.changeOption : 1
    readonly property int change: changeOption === 1 ? 0 : changeOption === 0 ? 1 : changeOption <= 3 ? 2 : 3
    readonly property var variants: change === 0 ? placingNames : change === 2 ? multiplyNames
                                   : change === 3 ? gradientNames : []
    readonly property int variant: change === 0 ? (editor ? editor.placing : 0)
                                 : change === 2 ? changeOption - 2 : change === 3 ? changeOption - 4 : -1
    function setChange(index) {
        editor.changeOption = [1, 0, 2, 4][index]
    }
    function setVariant(index) {
        if (change === 0)
            editor.placing = index
        else
            editor.changeOption = (change === 2 ? 2 : 4) + index
    }

    // MUST KILL: stepText's list is index-parallel to AllTagsEdition::Step
    function stepText(step) {
        return [qsTr("Rename tag"), qsTr("Edit tag"), qsTr("Edit range"), qsTr("Edit step"),
                qsTr("Edit decimal places"), qsTr("Change count"), qsTr("Edit starting value"),
                qsTr("Change change option"), qsTr("Change placing"), qsTr("New tag"), qsTr("Duplicate tag"),
                qsTr("Delete tag"), qsTr("Replace tag"), qsTr("Restore default tags"),
                qsTr("Undo all changes")][step] ?? ""
    }
    function checkText(code, field) {
        switch (code) {
        case AllTagsEditor.CheckEmpty:
            return field === "tag" ? qsTr("Type the tag, without the backslash.") : qsTr("Type a name.")
        case AllTagsEditor.CheckTaken:
            return qsTr("Another tag is named “%1”. Press Enter to replace it.").arg(editor.takenName())
        case AllTagsEditor.CheckTakenLastUsable:
            return qsTr("Another tag is named “%1”. Fill in Tag first to replace it.").arg(editor.takenName())
        case AllTagsEditor.CheckSeparator:
            return field === "tag" ? qsTr("A tag can't contain “,”.") : qsTr("A name can't contain “,”.")
        case AllTagsEditor.CheckNotANumber:
            return qsTr("Type a number.")
        case AllTagsEditor.CheckOutOfRange:
            return field === "digits" ? qsTr("Type a whole number from 0 to 6.")
                                      : qsTr("Type a number from -10000 to 10000.")
        case AllTagsEditor.CheckMaxNotAboveMin:
            return qsTr("The maximum must be greater than the minimum (%1).").arg(editor.limit())
        case AllTagsEditor.CheckMinNotBelowMax:
            return qsTr("The minimum must be less than the maximum (%1).").arg(editor.limit())
        case AllTagsEditor.CheckStepNotPositive:
            return qsTr("The step must be greater than zero.")
        case AllTagsEditor.CheckStepTooLarge:
            return qsTr("The step must be at most half the range (%1).").arg(editor.limit())
        }
        return ""
    }

    signal showFields()
    property bool asking: false
    function nameLeft() {
        if (!editor || asking || tagName.error.length === 0) {
            if (editor)
                editor.endTyping()
            return
        }
        if (!visible) {
            showFields()
            return
        }
        if (editor.setName(tagName.text) !== AllTagsEditor.CheckTaken) {
            showFields()
            return
        }
        const name = tagName.text
        asking = true
        messages.ask({
            title: dialog.title,
            text: qsTr("Replace the tag “%1”?").arg(editor.takenName()),
            detail: qsTr("“%1” takes its name and its place in the list; the other is removed. Undo brings it back while this window is open.").arg(editor.name),
            severity: "warning",
            choices: [{ id: "replace", text: qsTr("Replace"), role: "destructive" },
                      { id: "keep", text: qsTr("Keep editing"), role: "reject", isDefault: true }]
        }, id => {
            asking = false
            if (id === "replace") {
                editor.replaceNamed(name)
                showFields()
            } else {
                tagName.forceActiveFocus()
                tagName.selectAll()
            }
        })
    }
    function askRestore() {
        const own = editor.ownCount
        messages.ask({
            title: dialog.title,
            text: qsTr("Restore the default tags?"),
            detail: (own > 0 ? qsTr("Your %n tag(s) are removed and the 22 default tags come back, with their ranges and steps.", "", own)
                             : qsTr("The 22 default tags come back with their ranges and steps."))
                    + " " + qsTr("Undo brings yours back while this window is open."),
            severity: "warning",
            choices: [{ id: "restore", text: qsTr("Restore defaults"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "restore") {
                editor.restoreDefaults()
                showFields()
            }
        })
    }
    function undo() { if (editor) { editor.undo(); showFields() } }
    function redo() { if (editor) { editor.redo(); showFields() } }

    onEditorChanged: showFields()
    // MUST KILL: a hidden transientParent means the window went with its opener, not a close
    onClosing: if (editor && !(transientParent && !transientParent.visible)) editor.close()
    Connections {
        target: dialog.editor
        function onClosed() { dialog.close() }
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: dialog.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        onActivated: dialog.redo()
    }

    component EditField: TextField {
        id: edit
        property string field
        property string error
        selectByMouse: true
        Accessible.description: error
        function show() {
            text = !dialog.editor ? ""
                 : field === "name" ? dialog.editor.name
                 : field === "tag" ? dialog.editor.tag
                 : dialog.editor.numbers[field] ?? ""
            error = ""
        }
        onTextEdited: {
            const code = field === "name" ? dialog.editor.setName(text)
                       : field === "tag" ? dialog.editor.setTag(text)
                       : dialog.editor.setNumber(field, text)
            error = dialog.checkText(code, field)
        }
        function leave() {
            if (field === "name") {
                dialog.nameLeft()
                return
            }
            dialog.editor.endTyping()
            show()
        }
        Keys.onReturnPressed: event => { event.accepted = true; leave() }
        Keys.onEnterPressed: event => { event.accepted = true; leave() }
        onActiveFocusChanged: if (!activeFocus && dialog.editor) leave()
        Connections {
            target: dialog
            function onShowFields() { edit.show() }
        }
        Component.onCompleted: show()
    }
    component NumberEdit: EditField {
        Layout.preferredWidth: 80
        font.family: "monospace"
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    component ValueEdit: RowLayout {
        id: valueEdit
        property int at
        readonly property alias error: valueField.error
        readonly property string label: dialog.editor ? dialog.editor.valueNames[at] ?? "" : ""
        visible: dialog.editor ? at < dialog.editor.count : at === 0
        spacing: 6
        Label { text: valueEdit.label; color: Theme.muted }
        NumberEdit {
            id: valueField
            objectName: "tagValue" + valueEdit.at
            field: "value" + valueEdit.at
            Layout.preferredWidth: 70
            Accessible.name: valueEdit.label
        }
    }
    component RowLabel: Label {
        color: Theme.muted
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 80
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }
    component SectionTitle: Label {
        font.weight: Font.DemiBold
        Layout.topMargin: 6
    }
    component Rule: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.line
        Layout.topMargin: 6
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 680
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: Theme.field
            border.color: Theme.line
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                TextField {
                    id: search
                    objectName: "tagSearch"
                    Layout.fillWidth: true
                    Layout.margins: 6
                    placeholderText: qsTr("Search tags")
                    Accessible.name: qsTr("Search tags")
                    Keys.onReturnPressed: event => { event.accepted = true; tagList.selectRow(tagList.firstItemRow(0, 1)) }
                    Keys.onEnterPressed: event => { event.accepted = true; tagList.selectRow(tagList.firstItemRow(0, 1)) }
                    Keys.onDownPressed: tagList.forceActiveFocus()
                }
                ListView {
                    id: tagList
                    objectName: "tagList"
                    property bool moreOpen: false
                    model: {
                        const items = dialog.editor ? dialog.editor.items : []
                        const query = search.text.trim().toLowerCase()
                        const rows = []
                        if (query.length > 0) {
                            items.forEach((t, i) => {
                                if (t.name.toLowerCase().includes(query) || t.tag.toLowerCase().includes(query))
                                    rows.push({ header: false, index: i, name: t.name, tag: t.tag })
                            })
                            return rows
                        }
                        for (let section = 0; section < 3; ++section) {
                            const inSection = []
                            items.forEach((t, i) => {
                                if (t.section === section)
                                    inSection.push({ header: false, index: i, name: t.name, tag: t.tag })
                            })
                            if (inSection.length === 0)
                                continue
                            const folded = section === 1 && !moreOpen
                            rows.push({ header: true, section: section, count: inSection.length, folded: folded })
                            if (!folded)
                                rows.push(...inSection)
                        }
                        return rows
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Tags")
                    ScrollBar.vertical: ScrollBar {}
                    function rowOf(index) {
                        for (let r = 0; r < count; ++r)
                            if (!model[r].header && model[r].index === index)
                                return r
                        return -1
                    }
                    function firstItemRow(from, step) {
                        for (let r = from; r >= 0 && r < count; r += step)
                            if (!model[r].header)
                                return r
                        return -1
                    }
                    // Bound only: setting it after a model change crashes QQuickItemView when the window goes.
                    currentIndex: dialog.editor && count > 0 ? rowOf(dialog.editor.selection) : -1
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                    function selectRow(row) {
                        if (row < 0 || row >= count || model[row].header || !dialog.editor)
                            return
                        if (model[row].index !== dialog.editor.selection) {
                            dialog.editor.select(model[row].index)
                            dialog.showFields()
                        }
                    }
                    readonly property int selected: dialog.editor ? dialog.editor.selection : -1
                    onSelectedChanged: {
                        const items = dialog.editor ? dialog.editor.items : []
                        if (!moreOpen && selected >= 0 && selected < items.length && items[selected].section === 1
                                && search.text.length === 0)
                            moreOpen = true
                    }
                    Keys.onUpPressed: selectRow(firstItemRow(rowOf(selected) - 1, -1))
                    Keys.onDownPressed: selectRow(firstItemRow(rowOf(selected) + 1, 1))
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: modelData.header ? "tagSection" + modelData.section : "tagRow" + modelData.index
                        width: ListView.view.width
                        height: modelData.header ? 28 : 30
                        focusPolicy: Qt.NoFocus
                        highlighted: !modelData.header && ListView.isCurrentItem
                        Accessible.role: modelData.header ? Accessible.Button : Accessible.ListItem
                        Accessible.name: modelData.header ? dialog.sectionName(modelData.section) : modelData.name
                        Accessible.selected: highlighted
                        onClicked: {
                            if (modelData.header) {
                                if (modelData.section === 1)
                                    tagList.moreOpen = !tagList.moreOpen
                            } else {
                                tagList.selectRow(index)
                            }
                        }
                        background: Rectangle {
                            color: row.highlighted ? Theme.select : row.hovered && !row.modelData.header ? Theme.raised : "transparent"
                            Rectangle {
                                visible: row.highlighted
                                width: 3
                                height: parent.height
                                color: Theme.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Icon {
                                visible: row.modelData.header
                                iconRole: row.modelData.folded ? "chevron-right" : "chevron-down"
                                size: 14
                            }
                            Label {
                                text: row.modelData.header ? dialog.sectionName(row.modelData.section) : row.modelData.name
                                font.weight: row.modelData.header || row.highlighted ? Font.DemiBold : Font.Normal
                                color: row.modelData.header ? Theme.muted : Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: row.modelData.header ? row.modelData.section === 1 : true
                                text: row.modelData.header ? row.modelData.count
                                    : row.modelData.tag.length > 0 ? "\\" + row.modelData.tag : ""
                                color: Theme.muted
                                font.family: row.modelData.header ? Qt.application.font.family : "monospace"
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                RowLayout {
                    Layout.margins: 4
                    spacing: 2
                    IconToolButton {
                        objectName: "addTag"
                        iconRole: "add"
                        text: qsTr("New tag")
                        onClicked: {
                            search.text = ""
                            dialog.editor.addTag()
                            dialog.showFields()
                            tagCode.forceActiveFocus()
                        }
                    }
                    IconToolButton {
                        objectName: "duplicateTag"
                        iconRole: "duplicate"
                        text: qsTr("Duplicate")
                        onClicked: { dialog.editor.duplicate(); dialog.showFields() }
                    }
                    IconToolButton {
                        objectName: "deleteTag"
                        iconRole: "delete"
                        text: qsTr("Delete")
                        enabled: dialog.editor ? dialog.editor.canRemove : false
                        tip: enabled ? text : qsTr("The list keeps at least one tag")
                        onClicked: { dialog.editor.remove(); dialog.showFields() }
                    }
                    Item { Layout.fillWidth: true }
                    IconToolButton {
                        id: moreButton
                        objectName: "tagsMore"
                        iconRole: "panel-menu"
                        text: qsTr("More")
                        down: moreMenu.visible
                        onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                    }
                }
            }
        }

        ScrollView {
            id: detailScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 620
            contentWidth: availableWidth
            ColumnLayout {
                width: detailScroll.availableWidth - 40
                x: 20
                spacing: 8

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    RowLabel { text: qsTr("Name") }
                    EditField {
                        id: tagName
                        objectName: "tagsName"
                        field: "name"
                        Layout.preferredWidth: 220
                        Accessible.name: qsTr("Name")
                    }
                    Item { implicitWidth: 1; visible: tagNameError.visible }
                    FieldError { id: tagNameError; objectName: "tagsNameError"; text: tagName.error; Layout.fillWidth: true }
                    RowLabel { text: qsTr("Tag") }
                    RowLayout {
                        spacing: 0
                        Label { text: "\\"; color: Theme.muted; font.family: "monospace"; Layout.rightMargin: 2 }
                        EditField {
                            id: tagCode
                            objectName: "tagCode"
                            field: "tag"
                            font.family: "monospace"
                            Layout.preferredWidth: 160
                            Accessible.name: qsTr("Tag")
                        }
                    }
                    Item { implicitWidth: 1; visible: tagCodeError.visible }
                    FieldError { id: tagCodeError; objectName: "tagCodeError"; text: tagCode.error; Layout.fillWidth: true }
                    RowLabel { text: qsTr("Default") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ValueEdit { id: value0; at: 0 }
                        ValueEdit { id: value1; at: 1 }
                        ValueEdit { id: value2; at: 2 }
                        ValueEdit { id: value3; at: 3 }
                    }
                    Item { implicitWidth: 1; visible: valuesError.visible }
                    FieldError {
                        id: valuesError
                        objectName: "tagValuesError"
                        text: value0.error || value1.error || value2.error || value3.error
                        Layout.fillWidth: true
                    }
                }

                FormSection {
                    id: advanced
                    objectName: "tagsAdvanced"
                    title: qsTr("Advanced")
                    expanded: dialog.advancedOpen
                    Layout.topMargin: 6
                    onToggled: open => dialog.advancedOpen = open
                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        RowLabel { text: qsTr("Range") }
                        RowLayout {
                            spacing: 8
                            NumberEdit { id: rangeMin; objectName: "tagMin"; field: "min"; Accessible.name: qsTr("Minimum") }
                            Label { text: qsTr("to"); color: Theme.muted }
                            NumberEdit { id: rangeMax; objectName: "tagMax"; field: "max"; Accessible.name: qsTr("Maximum") }
                        }
                        Item { implicitWidth: 1; visible: rangeError.visible }
                        FieldError {
                            id: rangeError
                            objectName: "tagRangeError"
                            text: rangeMin.error || rangeMax.error
                            Layout.fillWidth: true
                        }
                        RowLabel { text: qsTr("Step") }
                        RowLayout {
                            spacing: 8
                            NumberEdit { id: stepField; objectName: "tagStep"; field: "step"; Accessible.name: qsTr("Step") }
                            Label { text: qsTr("Decimal places"); color: Theme.muted; Layout.leftMargin: 10 }
                            NumberEdit {
                                id: digitsField
                                objectName: "tagDigits"
                                field: "digits"
                                Layout.preferredWidth: 52
                                Accessible.name: qsTr("Decimal places")
                            }
                        }
                        Item { implicitWidth: 1; visible: stepError.visible }
                        FieldError {
                            id: stepError
                            objectName: "tagStepError"
                            text: stepField.error || digitsField.error
                            Layout.fillWidth: true
                        }
                        RowLabel { text: qsTr("Count") }
                        SegmentedButtons {
                            objectName: "tagCount"
                            choices: [1, 2, 3, 4].map(n => ({ text: String(n), name: "tagCount" + n,
                                                              tip: qsTr("The tag takes %n value(s)", "", n) }))
                            currentIndex: dialog.editor ? dialog.editor.count - 1 : 0
                            Accessible.name: qsTr("Count")
                            onActivated: index => dialog.editor.count = index + 1
                        }
                        RowLabel { text: qsTr("Starts on") }
                        ColumnLayout {
                            spacing: 8
                            SegmentedButtons {
                                objectName: "tagChange"
                                choices: dialog.changeNames.map((n, i) => ({ text: n, name: "tagChange" + i }))
                                currentIndex: dialog.change
                                Accessible.name: qsTr("Starts on")
                                onActivated: index => dialog.setChange(index)
                            }
                            ComboBox {
                                objectName: "tagVariant"
                                visible: dialog.variants.length > 0
                                model: dialog.variants
                                currentIndex: dialog.variant
                                Layout.preferredWidth: 280
                                Accessible.name: dialog.change === 0 ? qsTr("Where the tag goes in the text")
                                               : dialog.change === 2 ? qsTr("How each line's number changes the value")
                                               : qsTr("What the gradient runs across")
                                onActivated: index => dialog.setVariant(index)
                            }
                        }
                    }
                }

                Rule {}
                SectionTitle { text: qsTr("Preview") }
                Label { text: qsTr("In the inspector bar"); color: Theme.muted }
                Rectangle {
                    objectName: "tagStripPreview"
                    Layout.fillWidth: true
                    implicitHeight: strip.implicitHeight + 16
                    color: Theme.raised
                    border.color: Theme.line
                    clip: true
                    RowLayout {
                        id: strip
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        Icon { iconRole: "tool-all-tags"; size: 16 }
                        Label { text: dialog.editor ? dialog.editor.name : ""; font.weight: Font.DemiBold }
                        Label {
                            text: dialog.editor && dialog.editor.tag.length > 0 ? "\\" + dialog.editor.tag : ""
                            color: Theme.muted
                            font.family: "monospace"
                        }
                        Repeater {
                            model: dialog.editor ? dialog.editor.count : 0
                            RowLayout {
                                id: stripValue
                                required property int index
                                spacing: 4
                                Label { text: dialog.editor.valueNames[stripValue.index] ?? ""; color: Theme.muted }
                                Label { text: dialog.editor.numbers["value" + stripValue.index] ?? ""; font.family: "monospace" }
                            }
                        }
                        Label {
                            text: dialog.changeNames[dialog.change]
                                  + (dialog.variant >= 0 ? " · " + dialog.variants[dialog.variant] : "")
                            color: Theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
                Label { text: qsTr("Set writes, on the active line"); color: Theme.muted; Layout.topMargin: 4 }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: writes.implicitHeight + 16
                    color: Theme.field
                    border.color: Theme.line
                    Label {
                        id: writes
                        objectName: "tagSetPreview"
                        anchors.fill: parent
                        anchors.margins: 8
                        textFormat: Text.StyledText
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.family: "monospace"
                        function escaped(text) {
                            return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                        }
                        readonly property var preview: dialog.editor ? dialog.editor.preview : ({})
                        text: escaped(preview.before ?? "")
                              + "<b><font color=\"" + Theme.accent + "\">" + escaped(preview.tag ?? "") + "</font></b>"
                              + escaped(preview.after ?? "")
                    }
                }
                Item { implicitHeight: 8 }
            }
        }
    }

    function sectionName(section) {
        return [qsTr("Common"), qsTr("More"), qsTr("Your tags")][section] ?? ""
    }

    leadingActions: [
        IconTextButton {
            objectName: "tagsUndo"
            iconRole: "undo"
            text: qsTr("Undo")
            enabled: dialog.editor ? dialog.editor.canUndo : false
            tip: enabled ? qsTr("Undo “%1”").arg(dialog.stepText(dialog.editor.undoStep)) : ""
            onClicked: dialog.undo()
        },
        IconTextButton {
            objectName: "tagsRedo"
            iconRole: "redo"
            text: qsTr("Redo")
            enabled: dialog.editor ? dialog.editor.canRedo : false
            tip: enabled ? qsTr("Redo “%1”").arg(dialog.stepText(dialog.editor.redoStep)) : ""
            onClicked: dialog.redo()
        }
    ]
    buttons: PrimaryButton {
        id: closeButton
        objectName: "tagsClose"
        text: qsTr("Close")
        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        onClicked: dialog.close()
    }
    defaultButton: closeButton

    ShellMenu {
        id: moreMenu
        objectName: "tagsMoreMenu"
        ShellMenuItem {
            objectName: "tagsRestoreDefault"
            iconRole: "restore-default"
            text: qsTr("Restore default tags…")
            onTriggered: dialog.askRestore()
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "tagsUndoAll"
            iconRole: "undo"
            text: qsTr("Undo all changes")
            onTriggered: { dialog.editor.undoAll(); dialog.showFields() }
        }
    }

    MessageHost {
        id: messages
        objectName: "tagsMessages"
        settings: dialog.settings
    }
}
