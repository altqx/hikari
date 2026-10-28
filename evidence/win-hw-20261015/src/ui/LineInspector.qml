// E4: the Line editor's metadata row (legacy EditBox's BoxSizer2,
// EditBox.cpp:262-305): Comment, Layer, Start, End, Duration, the Style
// choice with its Edit button, Actor, the three margins and Effect. Below
// 850 pixels legacy moves Comment, Actor, the margins and Effect to a second
// row (EditBox::OnSize, EditBox.cpp:1374-1407); so does this one.
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
    readonly property bool wide: (parent ? parent.width : width) > 850
    spacing: 2

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
        color: pending ? Theme.warning : palette.text
        PendingMark {}
        Connections {
            target: inspector.editor
            function onDiscarded() {
                field.text = field.value
                field.given(field.value)
            }
        }
        Accessible.description: pending ? inspector.pendingText : ""
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: pending ? tip + "\n" + inspector.pendingText : tip
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
        Layout.preferredWidth: 90
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
        Layout.minimumWidth: 90
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

    RowLayout {
        id: firstRow
        Layout.fillWidth: true
        spacing: 2
    }
    RowLayout {
        id: secondRow
        Layout.fillWidth: true
        visible: !inspector.wide
        spacing: 2
    }
    // Where each control goes: the wide order in one row, or legacy's two
    // narrow rows. Parents are assigned in order, so the rows keep it.
    readonly property var wideOrder: [comment, layer, startField, endField, duration, styleChoice, styleEdit, actor,
                                      marginLeft, marginRight, marginVertical, effect]
    readonly property var narrowFirst: [layer, startField, endField, duration, styleChoice, styleEdit]
    readonly property var narrowSecond: [comment, actor, marginLeft, marginRight, marginVertical, effect]
    function arrange() {
        for (const item of wideOrder)
            item.parent = holder
        if (wide) {
            for (const item of wideOrder)
                item.parent = firstRow
        } else {
            for (const item of narrowFirst)
                item.parent = firstRow
            for (const item of narrowSecond)
                item.parent = secondRow
        }
    }
    onWideChanged: arrange()
    Component.onCompleted: arrange()
    Item {
        id: holder
        visible: false
        Layout.preferredWidth: 0
        Layout.preferredHeight: 0

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
        Field {
            id: layer
            objectName: "layerField"
            value: inspector.editor.layerText
            enabled: inspector.editor.editable && inspector.editor.assFields
            Layout.preferredWidth: 50
            maximumLength: 10 // NumCtrl::SetMaxLength
            validator: RegularExpressionValidator { regularExpression: /[-0-9.,]*/ }
            Accessible.name: qsTr("Layer")
            tip: qsTr("Line layer. Higher layers are on top")
            numberRole: 0
            from: -10000000
            to: 10000000
            onApplied: text => inspector.editor.setLayerText(text)
        }
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
        ComboBox {
            id: styleChoice
            objectName: "styleChoice"
            model: inspector.editor.styleNames
            currentIndex: inspector.editor.styleIndex
            enabled: inspector.editor.editable && inspector.editor.assFields
            focusPolicy: Qt.TabFocus
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 4
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
        ValueBox {
            id: actor
            objectName: "actorBox"
            value: inspector.editor.actor
            model: inspector.editor.actors
            placeholderText: qsTr("Actor")
            enabled: inspector.editor.editable && inspector.editor.assFields
            Layout.horizontalStretchFactor: 3
            Accessible.name: qsTr("Actor")
            tip: qsTr("Line actor label. Does not affect the appearance of the subtitles")
            onTyped: text => inspector.editor.setActorText(text)
            onChosen: text => inspector.editor.chooseActor(text)
        }
        Field {
            id: marginLeft
            objectName: "marginLeftField"
            value: inspector.editor.marginLeftText
            enabled: inspector.editor.editable && inspector.editor.assFields
            Layout.preferredWidth: 50
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
            Layout.preferredWidth: 50
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
            Layout.preferredWidth: 50
            Accessible.name: qsTr("Vertical margin")
            numberRole: 3
            from: 0
            to: 9999
            tip: qsTr("Line top and bottom margins")
            onApplied: text => inspector.editor.setMarginText(2, text)
        }
        ValueBox {
            id: effect
            objectName: "effectBox"
            value: inspector.editor.effect
            model: inspector.editor.effects
            placeholderText: qsTr("Effect")
            enabled: inspector.editor.editable && inspector.editor.assFields
            Layout.horizontalStretchFactor: 3
            Accessible.name: qsTr("Effect")
            tip: qsTr("Line effect. Used to mark lines to which karaoke or VSFilter effects should be applied")
            onTyped: text => inspector.editor.setEffectText(text)
            onChosen: text => inspector.editor.chooseEffect(text)
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
