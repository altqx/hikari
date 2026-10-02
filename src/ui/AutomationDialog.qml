// A script's dialog (N8): fixed controls placed on the script's grid cells.
// Values go back through the controller, which types them per control.
// Enter presses the first button; Escape presses the last (the legacy wx
// enter and escape buttons, since script button IDs are dropped); closing the
// window answers with no button.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Window {
    id: window
    required property AutomationDialogController controller

    title: controller.title
    visible: controller.open
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    width: Math.max(320, content.implicitWidth + 24)
    height: content.implicitHeight + 24

    function values() {
        const out = [];
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i);
            out.push(cell && cell.item ? cell.item.readValue() : undefined);
        }
        return out;
    }
    function press(index) { controller.finish(index, values()); }

    onClosing: function(close) {
        if (controller.open)
            controller.finish(-1, values());
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: window.controller.open && window.controller.buttons.length > 0
        onActivated: window.press(0)
    }
    Shortcut {
        sequence: "Escape"
        enabled: window.controller.open && window.controller.buttons.length > 0
        onActivated: window.press(window.controller.buttons.length - 1)
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        GridLayout {
            columns: window.controller.columns
            rowSpacing: 4
            columnSpacing: 4

            Repeater {
                id: cells
                model: window.controller.controls
                delegate: Loader {
                    required property var modelData
                    required property int index
                    objectName: "control_" + index
                    Layout.row: modelData.y
                    Layout.column: modelData.x
                    Layout.rowSpan: modelData.height
                    Layout.columnSpan: modelData.width
                    Layout.fillWidth: modelData.kind !== "label" && modelData.kind !== "checkbox"
                    sourceComponent: {
                        switch (modelData.kind) {
                        case "label": return labelControl;
                        case "textbox": return textboxControl;
                        case "intedit": return intControl;
                        case "floatedit": return floatControl;
                        case "dropdown": return dropdownControl;
                        case "checkbox": return checkControl;
                        case "color":
                        case "coloralpha": return colorControl;
                        default: return editControl; // edit, alpha
                        }
                    }
                    property var spec: modelData
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Repeater {
                model: window.controller.buttons
                delegate: Button {
                    required property string modelData
                    required property int index
                    objectName: "dialogButton" + index
                    text: modelData
                    onClicked: window.press(index)
                }
            }
        }
    }

    Component {
        id: labelControl
        Label {
            readonly property var spec: parent ? parent.spec : ({})
            text: spec.label
            function readValue() { return undefined; }
        }
    }
    Component {
        id: editControl
        TextField {
            readonly property var spec: parent ? parent.spec : ({})
            text: spec.text
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return text; }
        }
    }
    Component {
        id: textboxControl
        TextArea {
            readonly property var spec: parent ? parent.spec : ({})
            text: spec.text
            implicitHeight: Math.max(30, contentHeight + topPadding + bottomPadding)
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return text; }
        }
    }
    Component {
        id: intControl
        SpinBox {
            readonly property var spec: parent ? parent.spec : ({})
            editable: true
            from: spec.intMin
            to: spec.intMax
            value: spec.intValue
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return value; }
        }
    }
    Component {
        id: floatControl
        TextField {
            id: floatField
            readonly property var spec: parent ? parent.spec : ({})
            text: String(spec.number)
            // As legacy NumCtrl: digits with a point or comma; no step (legacy
            // ignores it); the controller clamps to the range.
            validator: RegularExpressionValidator { regularExpression: /^-?[0-9]*([.,][0-9]*)?$/ }
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return text; }
        }
    }
    Component {
        id: dropdownControl
        ComboBox {
            readonly property var spec: parent ? parent.spec : ({})
            model: spec.items
            currentIndex: spec.items.indexOf(spec.text)
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return currentIndex >= 0 ? currentText : spec.text; }
        }
    }
    Component {
        id: colorControl
        RowLayout {
            id: colorRow
            readonly property var spec: parent ? parent.spec : ({})
            property color value: spec.color
            function readValue() { return value; }
            Rectangle {
                implicitWidth: 28
                implicitHeight: 20
                color: colorRow.value
                border.color: palette.mid
                Accessible.name: colorRow.spec.hint || colorRow.spec.name
            }
            Button {
                objectName: "colorPick"
                text: "…"
                ToolTip.text: colorRow.spec.hint
                ToolTip.visible: hovered && colorRow.spec.hint !== ""
                onClicked: picker.open()
            }
            ColorDialog {
                id: picker
                selectedColor: colorRow.value
                options: colorRow.spec.kind === "coloralpha" ? ColorDialog.ShowAlphaChannel : 0
                onAccepted: colorRow.value = selectedColor
            }
        }
    }
    Component {
        id: checkControl
        CheckBox {
            readonly property var spec: parent ? parent.spec : ({})
            text: spec.label
            checked: spec.checked
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return checked; }
        }
    }
}
