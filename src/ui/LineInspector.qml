pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: inspector
    required property LineEditorController editor
    required property var hotkeys
    signal styleEditRequested(string style)

    readonly property bool wide: (parent ? parent.width : width) >= wideMinimum
    spacing: 6
    readonly property int slotSpacing: 8
    readonly property int timeWidth: 90
    readonly property int marginWidth: 56
    readonly property int marginMinimum: 48
    readonly property real wideMinimum: Math.max(rowMinimum(wideFirst), rowMinimum(wideSecond))
    function rowMinimum(slots) {
        let w = (slots.length - 1) * slotSpacing
        for (const slot of slots)
            w += slot.wideMinimum
        return w
    }
    readonly property real narrowColumn: Math.max(...narrowOrder.filter(s => s !== styleSlot).map(s => s.narrowMinimum))
    readonly property real minimumWidth: Math.max(2 * narrowColumn + narrowGrid.columnSpacing, styleSlot.narrowMinimum)

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

    readonly property string pendingText: qsTr("Changed, not applied to the line yet")
    component PendingMark: Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Theme.warning
        visible: parent.pending
    }

    component Field: NumberField {
        id: field
        property string value
        property string tip
        property string iconRole
        property string hint
        readonly property bool hinting: hint.length > 0 && text === "0" && !activeFocus && !pending
        // MUST KILL: numberRole is a magic int (0 Layer, 1-3 the margins)
        property int numberRole: -1
        signal applied(string text)
        text: value
        Layout.preferredWidth: 90
        onValueChanged: {
            text = value
            given(value)
        }
        Component.onCompleted: given(value)
        onStepped: {
            if (activeFocus)
                return
            inspector.editor.numberStepped(numberRole, text)
            text = value
            modified = false
        }
        function apply() {
            if (text !== value || modified)
                applied(text)
            modified = false
        }
        onEditingFinished: apply()
        Keys.onPressed: event => event.accepted = inspector.commitKey(event, field, false)
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
    }

    component TimeBox: TimeField {
        id: time
        property string value
        // MUST KILL: timeRole is a magic int (0 Duration, 1 Start, 2 End)
        property int timeRole: -1
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
        color: warning || pending ? Theme.warning : palette.text
        onValueChanged: {
            if (typing)
                return
            text = value
            modified = false
        }
        onTyped: {
            if (!activeFocus) {
                inspector.editor.timeStepped(timeRole, text, inspector.focusedTimeRole())
                text = value
                modified = false
                return
            }
            typing = true
            inspector.editor.timeTyped(timeRole, text)
            typing = false
        }
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
    }
    function focusedTimeRole() {
        return startField.activeFocus ? 1 : endField.activeFocus ? 2 : duration.activeFocus ? 0 : -1
    }

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
    }

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
    readonly property var wideFirst: [startSlot, endSlot, durationSlot, styleSlot]
    readonly property var wideSecond: [commentSlot, layerSlot, actorSlot, effectSlot, marginsSlot]
    readonly property var narrowOrder: [startSlot, endSlot, durationSlot, layerSlot, styleSlot, actorSlot, effectSlot,
                                        marginsSlot, commentSlot]
    // A layout orders its items by when they were parented, so every item goes through holder first.
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
            minimum: layerField.Layout.minimumWidth
            Field {
                id: layerField
                objectName: "layerField"
                value: inspector.editor.layerText
                enabled: inspector.editor.editable && inspector.editor.assFields
                Layout.preferredWidth: 50
                Layout.minimumWidth: 40
                Layout.maximumWidth: inspector.wide ? 50 : Number.POSITIVE_INFINITY
                Layout.fillWidth: true
                maximumLength: 10
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

    Connections {
        target: inspector.editor
        function onSelectionRequested() {
            const box = inspector.editor.selectionRole === 2 ? actor : inspector.editor.selectionRole === 3 ? effect : null
            if (box)
                box.contentItem.select(inspector.editor.selectionStart, inspector.editor.selectionEnd)
        }
    }
}
