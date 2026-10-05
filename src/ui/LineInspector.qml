// E4: the Line editor's metadata row (legacy EditBox's BoxSizer2,
// EditBox.cpp:262-305): Comment, Layer, Start, End, Duration, the Style
// choice with its Edit button, Actor, the three margins and Effect. Below
// 850 pixels legacy moves Comment, Actor, the margins and Effect to a second
// row (EditBox::OnSize, EditBox.cpp:1374-1407); so does this one.
//
// Fields change the editor's draft when their editing finishes; with live
// video editing on, Start, End and Duration also change it as they are typed
// (TimeCtrl's NUMBER_CHANGED runs EditBox::OnEdit, EditBox.cpp:343-346), and
// Start or End show the warning colour while Start is after End. Enter runs
// the Editor's EDITBOX_COMMIT_GO_NEXT_LINE / EDITBOX_COMMIT bindings (the
// time fields stay on the Line with EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,
// EditBox::OnNewline). The Comment box, the Style choice and a pick from the
// Actor or Effect list send the Line at once (EditBox.cpp:322-325).
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

    readonly property bool wide: width > 850
    spacing: 2

    // Enter and Ctrl+Enter as the Editor binds them (EditBox's accelerators).
    function commitKey(event, field, timeField) {
        const action = inspector.hotkeys.actionFor(2, event.key, event.modifiers, false)
        if (action !== "EDITBOX_COMMIT_GO_NEXT_LINE" && action !== "EDITBOX_COMMIT")
            return false
        field.apply()
        if (action === "EDITBOX_COMMIT")
            inspector.editor.commit()
        else
            inspector.editor.commitFromField(timeField)
        return true
    }

    component Field: TextField {
        id: field
        property string value
        property bool timeField: false
        property int timeRole: -1 // TimeFieldRole: 0 Duration, 1 Start, 2 End
        property bool warning: false
        property bool typing: false
        property string tip
        signal applied(string text)
        text: value
        selectByMouse: true
        Layout.preferredWidth: 90
        // Legacy WINDOW_WARNING_ELEMENTS: the theme layer's warning role.
        color: warning ? Theme.warning : palette.text
        // A live edit leaves the typed text as it is (OnEdit sets the other
        // fields, not the focused one).
        onValueChanged: if (!typing) text = value
        onTextEdited: {
            if (!timeField)
                return
            typing = true
            inspector.editor.timeTyped(timeRole, text)
            typing = false
        }
        // Only a changed field is sent (legacy IsModified). An applied time
        // field shows the Line's form again, the only form TimeCtrl holds.
        function apply() {
            if (text !== value)
                applied(text)
            if (timeField && inspector.editor.attempted.length === 0)
                text = value
        }
        onEditingFinished: apply()
        Keys.onPressed: event => event.accepted = inspector.commitKey(event, field, field.timeField)
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 500
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
            onApplied: text => inspector.editor.setLayerText(text)
        }
        Field {
            id: startField
            objectName: "startField"
            value: inspector.editor.startText
            enabled: inspector.editor.editable
            timeField: true
            timeRole: 1
            warning: inspector.editor.startWarning
            Accessible.name: qsTr("Start")
            tip: qsTr("Line start time")
            onApplied: text => inspector.editor.setStartText(text)
        }
        Field {
            id: endField
            objectName: "endField"
            value: inspector.editor.endText
            enabled: inspector.editor.editable && inspector.editor.hasEnd
            timeField: true
            timeRole: 2
            warning: inspector.editor.endWarning
            Accessible.name: qsTr("End")
            tip: qsTr("Line end time")
            onApplied: text => inspector.editor.setEndText(text)
        }
        Field {
            id: duration
            objectName: "durationField"
            value: inspector.editor.durationText
            enabled: inspector.editor.editable && inspector.editor.hasEnd
            timeField: true
            timeRole: 0
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
