// E4: the Line editor's metadata fields (legacy EditBox's BoxSizer2,
// EditBox.cpp:262-305): Comment, Layer, Start, End, Duration, the Style
// choice with its Edit button, Actor, the three margins and Effect.
// LE1-inspector-rows (#233): each field shows its name, beside it while the
// rows fit, above it in a narrow editor (MuseScore's Properties panel). Wide:
// Start, End, Duration and Style in one row, Comment, Layer, Actor, Effect
// and the margins in the next. Narrow: two to a row (Start and End,
// Duration and Layer, Style alone, Actor and Effect, the margins and
// Comment). Legacy's one row above 850 pixels and its two unnamed rows below
// (EditBox::OnSize, EditBox.cpp:1374-1407) are gone. The margins share one
// name, each field marked by its edge's icon; a margin of 0 (the Style's)
// shows the Style's margin in the secondary colour while the field is not
// being edited, its text still 0.
//
// Fields change the editor's draft when their editing finishes; with live
// video editing on, Start, End and Duration also change it as they are typed
// (TimeCtrl's NUMBER_CHANGED runs EditBox::OnEdit, EditBox.cpp:343-346), and
// Start or End show the warning colour while Start is after End; times the
// audio display put in the fields show it until the Line takes them. The
// mouse wheel over a field without the focus changes only that field, which
// the next Send takes and showing another Line drops (legacy IsModified);
// such a field shows the warning colour until then. Enter runs the Editor's
// EDITBOX_COMMIT_GO_NEXT_LINE / EDITBOX_COMMIT bindings (the time fields
// stay on the Line with EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,
// EditBox::OnNewline). The Comment box, the Style choice and a pick from the
// Actor or Effect list send the Line at once (EditBox.cpp:322-325).
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: inspector
    required property LineEditorController editor
    required property var hotkeys
    // The Edit button (legacy OnStyleEdit): the Style manager on that Style.
    signal styleEditRequested(string style)

    // The column's width, not its own: laid out at its one-row size, the
    // inspector would stay wide in a column that has narrowed (D3).
    readonly property bool wide: (parent ? parent.width : width) >= wideMinimum
    spacing: 6
    readonly property int slotSpacing: 8 // between named fields in a row
    readonly property int timeWidth: 90 // Start, End and Duration (TimeCtrl's text)
    readonly property int marginWidth: 56
    readonly property int marginMinimum: 48
    // The narrowest the two rows get, each field at its minimum.
    readonly property real wideMinimum: Math.max(rowMinimum(wideFirst), rowMinimum(wideSecond))
    function rowMinimum(slots) {
        let w = (slots.length - 1) * slotSpacing
        for (const slot of slots)
            w += slot.wideMinimum
        return w
    }
    // The narrow grid's columns, equal, and its width (the Line editor's
    // minimum).
    readonly property real narrowColumn: Math.max(...narrowOrder.filter(s => s !== styleSlot).map(s => s.narrowMinimum))
    readonly property real minimumWidth: Math.max(2 * narrowColumn + narrowGrid.columnSpacing, styleSlot.narrowMinimum)

    // Enter and Ctrl+Enter as the Editor binds them (EditBox's accelerators).
    function commitKey(event, field, timeField) {
        const action = inspector.hotkeys.actionFor(2, event.key, event.modifiers, false)
        if (action !== "EDITBOX_COMMIT_GO_NEXT_LINE" && action !== "EDITBOX_COMMIT")
            return false
        field.apply()
        if (action === "EDITBOX_COMMIT")
            inspector.editor.applyChanges()
        else
            inspector.editor.commitFromField(timeField)
        return true
    }

    // E4-stepped-field-warning: a field holding a mouse step that the Line
    // has not taken shows the theme's warning colour, its text and a 1-pixel
    // outline over the field's own, until a Send takes the step or showing
    // another Line, Esc, the Time/Frames switch or a time from the audio
    // display drops it; screen readers read the state as the field's
    // description, and its tooltip says it too.
    readonly property string pendingText: qsTr("Changed, not applied to the line yet")
    component PendingMark: Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Theme.warning
        visible: parent.pending
    }

    // The Layer and the margins: legacy NumCtrl (NumberField, with its mouse).
    component Field: NumberField {
        id: field
        property string value
        property string tip
        // LE1: a margin's edge icon, and the Style's margin shown for 0.
        property string iconRole
        property string hint
        readonly property bool hinting: hint.length > 0 && text === "0" && !activeFocus && !pending
        property int numberRole: -1 // the editor's NumberFieldRole: 0 Layer, 1-3 the margins
        signal applied(string text)
        text: value
        Layout.preferredWidth: 90
        onValueChanged: {
            text = value
            given(value)
        }
        Component.onCompleted: given(value)
        // The wheel steps a field without the focus: modified, kept until
        // the next Send (legacy IsModified), as the time fields.
        onStepped: {
            if (activeFocus)
                return
            inspector.editor.numberStepped(numberRole, text)
            text = value
            modified = false
        }
        // Only a changed or stepped field is sent (legacy IsModified).
        function apply() {
            if (text !== value || modified)
                applied(text)
            modified = false
        }
        onEditingFinished: apply()
        Keys.onPressed: event => event.accepted = inspector.commitKey(event, field, false)
        // A step not yet sent: the warning colour (E4-stepped-field-warning).
        readonly property bool pending: modified || (inspector.editor.steppedNumbers & (1 << numberRole)) !== 0
        color: hinting ? "transparent" : pending ? Theme.warning : palette.text
        PendingMark {}
        leftPadding: iconRole.length > 0 ? 4 + 16 + 2 : padding
        rightPadding: iconRole.length > 0 ? 4 : padding
        Icon {
            visible: field.iconRole.length > 0
            iconRole: field.iconRole
            x: 4
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            objectName: field.objectName + "Hint"
            visible: field.hinting
            x: field.leftPadding
            anchors.verticalCenter: parent.verticalCenter
            text: field.hint
            font: field.font
            color: Theme.muted
        }
        Connections {
            target: inspector.editor
            function onDiscarded() {
                field.text = field.value
                field.given(field.value)
            }
        }
        readonly property string hintText: hinting ? qsTr("0: the Style's (%1)").arg(hint) : ""
        Accessible.description: pending ? inspector.pendingText : hintText
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: pending ? tip + "\n" + inspector.pendingText : hinting ? tip + "\n" + hintText : tip
        ToolTip.delay: 500
    }

    // Start, End and Duration: legacy TimeCtrl's masked field (TimeField),
    // in the form the Document and the Time/Frames switch give it.
    component TimeBox: TimeField {
        id: time
        property string value
        property int timeRole: -1 // TimeFieldRole: 0 Duration, 1 Start, 2 End
        property bool warning: false
        property bool typing: false
        property string tip
        signal applied(string text)
        form: inspector.editor.timeForm
        mouseForm: inspector.editor.timeCtrlForm
        frames: inspector.editor.framesShown
        text: value
        Layout.preferredWidth: inspector.timeWidth
        Layout.minimumWidth: inspector.timeWidth
        Layout.fillWidth: !inspector.wide
        // Legacy WINDOW_WARNING_ELEMENTS: the theme layer's warning role,
        // also while the field holds a mouse step not yet sent
        // (E4-stepped-field-warning).
        color: warning || pending ? Theme.warning : palette.text
        // A live edit leaves the typed text as it is (OnEdit sets the other
        // fields, not the focused one).
        onValueChanged: {
            if (typing)
                return
            text = value
            modified = false
        }
        onTyped: {
            // The wheel steps a field without the focus, whose editing then
            // never finishes: the field is modified and keeps its text until
            // the next Send takes it (legacy IsModified), which the editor
            // holds. OnEdit acts on the focused time field only
            // (EditBox.cpp:1524-1525), with this field's text read as it is.
            if (!activeFocus) {
                inspector.editor.timeStepped(timeRole, text, inspector.focusedTimeRole())
                text = value // what the editor kept: the step, or the Line's time OnEdit put back
                modified = false
                return
            }
            typing = true
            inspector.editor.timeTyped(timeRole, text)
            typing = false
        }
        // Only a changed or stepped field is sent (legacy IsModified). An
        // applied time field shows the Line's form again, the only form
        // TimeCtrl holds.
        function apply() {
            if (text !== value || modified)
                applied(text)
            modified = false
            if (inspector.editor.attempted.length === 0)
                text = value
        }
        onEditingFinished: apply()
        otherKey: event => inspector.commitKey(event, time, true)
        readonly property bool pending: modified || (inspector.editor.steppedTimes & (1 << timeRole)) !== 0
        PendingMark {}
        Connections {
            target: inspector.editor
            function onDiscarded() {
                time.text = time.value
                time.modified = false
            }
        }
        Accessible.description: pending ? inspector.pendingText : ""
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: pending ? tip + "\n" + inspector.pendingText : tip
        ToolTip.delay: 500
    }
    // The time field with the focus (TimeFieldRole), or -1.
    function focusedTimeRole() {
        return startField.activeFocus ? 1 : endField.activeFocus ? 2 : duration.activeFocus ? 0 : -1
    }

    // Legacy ComboBoxCtrl: an editable box listing the Document's values,
    // with its name shown while empty. Typing changes the draft; a pick from
    // the list sends the Line. Commas cannot be typed (its validator).
    component ValueBox: ComboBox {
        id: box
        property string value
        property string tip
        property string placeholderText
        property bool syncing: false
        signal typed(string text)
        signal chosen(string text)
        editable: true
        currentIndex: -1
        Layout.fillWidth: true
        Layout.minimumWidth: 56
        validator: RegularExpressionValidator { regularExpression: /[^,]*/ }
        function sync() {
            if (editText === value)
                return
            syncing = true
            editText = value
            syncing = false
        }
        function apply() {
            if (editText !== value)
                typed(editText)
        }
        onValueChanged: sync()
        onModelChanged: Qt.callLater(sync)
        Component.onCompleted: sync()
        onActivated: index => {
            chosen(textAt(index))
            currentIndex = -1
            Qt.callLater(sync)
        }
        contentItem: TextField {
            objectName: box.objectName + "Text"
            text: box.editText
            placeholderText: box.placeholderText
            validator: box.validator
            selectByMouse: true
            background: null
            onTextEdited: if (!box.syncing) box.typed(text)
            Keys.onPressed: event => event.accepted = inspector.commitKey(event, box, false)
        }
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    // A field and its name: beside it in the rows, above it in the grid.
    // `minimum` is the controls' narrowest.
    component Slot: GridLayout {
        id: slot
        property string label
        property real minimum
        default property alias controls: box.data
        readonly property real wideMinimum: (label.length > 0 ? name.implicitWidth + columnSpacing : 0) + minimum
        readonly property real narrowMinimum: Math.max(name.implicitWidth, minimum)
        columns: inspector.wide ? 2 : 1
        columnSpacing: 5
        rowSpacing: 2
        Label {
            id: name
            objectName: slot.objectName + "Label"
            visible: slot.label.length > 0
            text: slot.label
            color: Theme.muted
            Layout.alignment: inspector.wide ? Qt.AlignVCenter : Qt.AlignLeft
        }
        RowLayout {
            id: box
            spacing: 2
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }

    RowLayout {
        id: firstRow
        Layout.fillWidth: true
        visible: inspector.wide
        spacing: inspector.slotSpacing
    }
    RowLayout {
        id: secondRow
        Layout.fillWidth: true
        visible: inspector.wide
        spacing: inspector.slotSpacing
    }
    GridLayout {
        id: narrowGrid
        Layout.fillWidth: true
        visible: !inspector.wide
        columns: 2
        columnSpacing: 8
        rowSpacing: 6
    }
    // Where each named field goes: the two wide rows, or the narrow grid.
    // Parents are assigned in order, so the rows keep it.
    readonly property var wideFirst: [startSlot, endSlot, durationSlot, styleSlot]
    readonly property var wideSecond: [commentSlot, layerSlot, actorSlot, effectSlot, marginsSlot]
    readonly property var narrowOrder: [startSlot, endSlot, durationSlot, layerSlot, styleSlot, actorSlot, effectSlot,
                                        marginsSlot, commentSlot]
    function arrange() {
        for (const item of narrowOrder)
            item.parent = holder
        if (wide) {
            for (const item of wideFirst)
                item.parent = firstRow
            for (const item of wideSecond)
                item.parent = secondRow
        } else {
            for (const item of narrowOrder)
                item.parent = narrowGrid
        }
    }
    onWideChanged: arrange()
    Component.onCompleted: arrange()
    // In the grid the halves are equal; Style takes the whole row. In a
    // row the ones that do not grow give way down to their minimum, which
    // the wide test counts (a layout keeps an item that does not fill at its
    // preferred width).
    component Half: Slot {
        Layout.fillWidth: true
        Layout.maximumWidth: inspector.wide && !grows ? implicitWidth : Number.POSITIVE_INFINITY
        Layout.minimumWidth: inspector.wide ? wideMinimum : inspector.narrowColumn
        Layout.preferredWidth: inspector.wide ? -1 : inspector.narrowColumn
        property bool grows: false
    }
    Item {
        id: holder
        visible: false
        Layout.preferredWidth: 0
        Layout.preferredHeight: 0

        Half {
            id: commentSlot
            objectName: "commentSlot"
            minimum: comment.implicitWidth
            Layout.alignment: inspector.wide ? Qt.AlignVCenter : Qt.AlignBottom
            CheckBox {
                id: comment
                objectName: "commentBox"
                text: qsTr("Comment")
                checked: inspector.editor.comment
                enabled: inspector.editor.editable && inspector.editor.assFields
                focusPolicy: Qt.TabFocus
                Accessible.name: text
                Accessible.description: qsTr("Sets the line as a comment. Comments are not shown")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.description
                ToolTip.delay: 500
                onToggled: {
                    inspector.editor.setComment(checked)
                    checked = Qt.binding(() => inspector.editor.comment)
                }
            }
        }
        Half {
            id: layerSlot
            objectName: "layerSlot"
            label: qsTr("Layer")
            minimum: layer.Layout.minimumWidth
            Field {
                id: layer
                objectName: "layerField"
                value: inspector.editor.layerText
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.preferredWidth: 50
                Layout.minimumWidth: 40
                Layout.maximumWidth: inspector.wide ? 50 : Number.POSITIVE_INFINITY
                Layout.fillWidth: true
                maximumLength: 10 // NumCtrl::SetMaxLength
                validator: RegularExpressionValidator { regularExpression: /[-0-9.,]*/ }
                Accessible.name: qsTr("Layer")
                tip: qsTr("Line layer. Higher layers are on top")
                numberRole: 0
                from: -10000000
                to: 10000000
                onApplied: text => inspector.editor.setLayerText(text)
            }
        }
        Half {
            id: startSlot
            objectName: "startSlot"
            label: qsTr("Start")
            minimum: inspector.timeWidth
            TimeBox {
                id: startField
                objectName: "startField"
                value: inspector.editor.startText
                enabled: inspector.editor.editable
                timeRole: 1
                warning: inspector.editor.startWarning
                Accessible.name: qsTr("Start")
                tip: qsTr("Line start time")
                onApplied: text => inspector.editor.setStartText(text)
            }
        }
        Half {
            id: endSlot
            objectName: "endSlot"
            label: qsTr("End")
            minimum: inspector.timeWidth
            TimeBox {
                id: endField
                objectName: "endField"
                value: inspector.editor.endText
                enabled: inspector.editor.editable && inspector.editor.hasEnd
                timeRole: 2
                warning: inspector.editor.endWarning
                Accessible.name: qsTr("End")
                tip: qsTr("Line end time")
                onApplied: text => inspector.editor.setEndText(text)
            }
        }
        Half {
            id: durationSlot
            objectName: "durationSlot"
            label: qsTr("Duration")
            minimum: inspector.timeWidth
            TimeBox {
                id: duration
                objectName: "durationField"
                value: inspector.editor.durationText
                enabled: inspector.editor.editable && inspector.editor.hasEnd
                timeRole: 0
                warning: inspector.editor.durationWarning
                Accessible.name: qsTr("Duration")
                tip: qsTr("Line duration")
                onApplied: text => inspector.editor.setDurationText(text)
            }
        }
        Slot {
            id: styleSlot
            objectName: "styleSlot"
            label: qsTr("Style")
            minimum: styleChoice.Layout.minimumWidth + 2 + styleEdit.implicitWidth
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 4
            Layout.minimumWidth: inspector.wide ? wideMinimum : narrowMinimum
            Layout.columnSpan: inspector.wide ? 1 : 2
            ComboBox {
                id: styleChoice
                objectName: "styleChoice"
                model: inspector.editor.styleNames
                currentIndex: inspector.editor.styleIndex
                enabled: inspector.editor.editable && inspector.editor.assFields
                focusPolicy: Qt.TabFocus
                Layout.fillWidth: true
                Layout.minimumWidth: 100
                Accessible.name: qsTr("Style")
                Accessible.description: qsTr("Line style")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.description
                ToolTip.delay: 500
                onActivated: index => {
                    inspector.editor.chooseStyle(textAt(index))
                    currentIndex = Qt.binding(() => inspector.editor.styleIndex)
                }
            }
            IconToolButton {
                id: styleEdit
                objectName: "styleEditButton"
                iconRole: "edit"
                text: qsTr("Edit style")
                tip: qsTr("Edit style: allows quick editing of the current line's style")
                enabled: inspector.editor.hasLine && inspector.editor.assFields
                focusPolicy: Qt.TabFocus
                Accessible.description: qsTr("Allows quick editing of the current line's style")
                ToolTip.delay: 500
                onClicked: inspector.styleEditRequested(inspector.editor.style)
            }
        }
        Half {
            id: actorSlot
            objectName: "actorSlot"
            label: qsTr("Actor")
            minimum: actor.Layout.minimumWidth
            grows: true
            Layout.horizontalStretchFactor: 3
            ValueBox {
                id: actor
                objectName: "actorBox"
                value: inspector.editor.actor
                model: inspector.editor.actors
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.horizontalStretchFactor: 3
                Accessible.name: qsTr("Actor")
                tip: qsTr("Line actor label. Does not affect the appearance of the subtitles")
                onTyped: text => inspector.editor.setActorText(text)
                onChosen: text => inspector.editor.chooseActor(text)
            }
        }
        Half {
            id: effectSlot
            objectName: "effectSlot"
            label: qsTr("Effect")
            minimum: effect.Layout.minimumWidth
            grows: true
            Layout.horizontalStretchFactor: 3
            ValueBox {
                id: effect
                objectName: "effectBox"
                value: inspector.editor.effect
                model: inspector.editor.effects
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.horizontalStretchFactor: 3
                Accessible.name: qsTr("Effect")
                tip: qsTr("Line effect. Used to mark lines to which karaoke or VSFilter effects should be applied")
                onTyped: text => inspector.editor.setEffectText(text)
                onChosen: text => inspector.editor.chooseEffect(text)
            }
        }
        Half {
            id: marginsSlot
            objectName: "marginsSlot"
            label: qsTr("Margins")
            minimum: 3 * inspector.marginMinimum + 2 * 2
            Field {
                id: marginLeft
                objectName: "marginLeftField"
                value: inspector.editor.marginLeftText
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.preferredWidth: inspector.marginWidth
                Layout.minimumWidth: inspector.marginMinimum
                Layout.maximumWidth: inspector.wide ? inspector.marginWidth : Number.POSITIVE_INFINITY
                Layout.fillWidth: true
                iconRole: "margin-left"
                hint: inspector.editor.styleMargins[0] ?? ""
                Accessible.name: qsTr("Left margin")
                numberRole: 1
                from: 0
                to: 9999
                tip: qsTr("Line left margin")
                onApplied: text => inspector.editor.setMarginText(0, text)
            }
            Field {
                id: marginRight
                objectName: "marginRightField"
                value: inspector.editor.marginRightText
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.preferredWidth: inspector.marginWidth
                Layout.minimumWidth: inspector.marginMinimum
                Layout.maximumWidth: inspector.wide ? inspector.marginWidth : Number.POSITIVE_INFINITY
                Layout.fillWidth: true
                iconRole: "margin-right"
                hint: inspector.editor.styleMargins[1] ?? ""
                Accessible.name: qsTr("Right margin")
                numberRole: 2
                from: 0
                to: 9999
                tip: qsTr("Line right margin")
                onApplied: text => inspector.editor.setMarginText(1, text)
            }
            Field {
                id: marginVertical
                objectName: "marginVerticalField"
                value: inspector.editor.marginVerticalText
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.preferredWidth: inspector.marginWidth
                Layout.minimumWidth: inspector.marginMinimum
                Layout.maximumWidth: inspector.wide ? inspector.marginWidth : Number.POSITIVE_INFINITY
                Layout.fillWidth: true
                iconRole: "margin-vertical"
                hint: inspector.editor.styleMargins[2] ?? ""
                Accessible.name: qsTr("Vertical margin")
                numberRole: 3
                from: 0
                to: 9999
                tip: qsTr("Line top and bottom margins")
                onApplied: text => inspector.editor.setMarginText(2, text)
            }
        }
    }

    // F1: a match in the Actor or Effect field selects it there
    // (ComboBoxCtrl::SetTextSelection, findreplace.cpp:366-371).
    Connections {
        target: inspector.editor
        function onSelectionRequested() {
            const box = inspector.editor.selectionRole === 2 ? actor : inspector.editor.selectionRole === 3 ? effect : null
            if (box)
                box.contentItem.select(inspector.editor.selectionStart, inspector.editor.selectionEnd)
        }
    }
}
