// A script's dialog (N8): fixed controls placed on the script's grid cells.
// Values go back through the controller, which types them per control.
// Closing the window answers with no button.
//
// Keys follow the legacy dialog (HikariDialog and the Hikari controls at
// 20d647c4); script button IDs are dropped, so the first button is the
// enter button:
// - the enter button has the focus when the dialog shows (HikariDialog::Show);
// - Return presses the focused button (MappedButton's Return accelerator) and,
//   with a checkbox or a dropdown focused, ends the dialog without a button
//   (HikariDialog::OnCharHook clicks the enter ID on the dialog itself, whose
//   OnEnter ends it with no button pushed: the script gets false); Escape ends
//   it without a button wherever the focus is, except in a text field;
// - in a text field (edit, textbox, int and float edits) Return and Escape go
//   to the field (OnCharHook skips them for a HikariTextCtrl): a single-line
//   field does nothing, the textbox takes Return as a new line;
// - Space does nothing on a button, a colour button, a checkbox or a dropdown
//   (none handles keys);
// - the arrows on a control that does not take them (buttons, colour buttons,
//   checkboxes, Left/Right on a dropdown) move the focus: on Windows to the
//   previous (Up/Left) or next (Down/Right) control in creation order,
//   wrapping (wx's dialog navigation, HikariDialog::SetNextControl); elsewhere
//   GTK's directional focus (the nearest control in that direction that
//   overlaps the focused one), as the Linux build's GtkWindow does;
// - Return with a colour button focused does nothing on Windows (its
//   accelerator is not handled) and opens the colour picker elsewhere (wxGTK
//   retries an unhandled accelerator as a button click);
// - keypad Enter matches the Return accelerators on Windows only.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel // K2: a Window draws white unless told
    id: window
    required property AutomationDialogController controller
    // Which legacy build's keyboard navigation to follow ("windows" or not).
    property string navigationPlatform: Qt.platform.os

    title: controller.title
    visible: controller.open
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    width: Math.max(320, content.implicitWidth + 24)
    height: content.implicitHeight + 24

    readonly property bool windowsKeys: navigationPlatform === "windows"

    function values() {
        const out = [];
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i);
            out.push(cell && cell.item ? cell.item.readValue() : undefined);
        }
        return out;
    }
    function press(index) { controller.finish(index, values()); }
    // HikariDialog::OnEnter / OnEscape: EndModal with no button pushed.
    function endWithoutButton() { controller.finish(-1, values()); }
    // Legacy HikariDialog::Show: FindWindow(enterId)->SetFocus(), where
    // LuaDialog::CreateWindow made the first OK/Yes/Save button the enter
    // button, else the first button; without the IDs, the first.
    function focusEnterButton() {
        const button = buttonRepeater.itemAt(0);
        if (button)
            button.forceActiveFocus(Qt.OtherFocusReason);
    }
    onVisibleChanged: if (visible) Qt.callLater(focusEnterButton)

    onClosing: function(close) {
        if (controller.open)
            controller.finish(-1, values());
    }

    function unmodified(event) { return (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier; }

    // The keyboard-focusable controls in legacy creation order: the controls
    // in the script's order, then the buttons.
    function focusables() {
        const out = [];
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i);
            if (cell && cell.item && cell.item.focusTarget)
                out.push(cell.item.focusTarget);
        }
        for (let i = 0; i < buttonRepeater.count; ++i)
            out.push(buttonRepeater.itemAt(i));
        return out;
    }
    function rect(item) {
        const p = item.mapToItem(window.contentItem, 0, 0);
        return { x: Math.round(p.x), y: Math.round(p.y), w: Math.round(item.width), h: Math.round(item.height) };
    }
    // GTK 3 gtk_container_focus_sort_up_down / _left_right and
    // gtk_container_focus_move over one container: candidates must overlap
    // the focused control across the direction and lie on its side; they are
    // sorted by their centres along it (ties by distance from the focused
    // control's centre), and the first after the focused control takes the
    // focus. Without one the focus stays.
    function gtkDirectional(from, key) {
        const vertical = key === Qt.Key_Up || key === Qt.Key_Down;
        const reverse = key === Qt.Key_Up || key === Qt.Key_Left;
        const o = rect(from);
        const list = [];
        for (const item of focusables()) {
            if (!item.visible || !item.enabled)
                continue;
            const c = rect(item);
            if (item !== from) {
                if (vertical) {
                    if (c.x + c.w <= o.x || c.x >= o.x + o.w)
                        continue;
                    if (key === Qt.Key_Down && c.y + c.h < o.y + o.h)
                        continue;
                    if (key === Qt.Key_Up && c.y > o.y)
                        continue;
                } else {
                    if (c.y + c.h <= o.y || c.y >= o.y + o.h)
                        continue;
                    if (key === Qt.Key_Right && c.x + c.w < o.x + o.w)
                        continue;
                    if (key === Qt.Key_Left && c.x > o.x)
                        continue;
                }
            }
            list.push({ item: item, r: c });
        }
        const cx = vertical ? Math.trunc((o.x + o.x + o.w) / 2) : o.x + Math.trunc(o.w / 2);
        const cy = vertical ? o.y + Math.trunc(o.h / 2) : Math.trunc((o.y + o.y + o.h) / 2);
        list.sort(function(a, b) {
            const a1 = vertical ? a.r.y + Math.trunc(a.r.h / 2) : a.r.x + Math.trunc(a.r.w / 2);
            const b1 = vertical ? b.r.y + Math.trunc(b.r.h / 2) : b.r.x + Math.trunc(b.r.w / 2);
            if (a1 !== b1)
                return a1 < b1 ? -1 : 1;
            const a2 = vertical ? Math.abs(a.r.x + Math.trunc(a.r.w / 2) - cx) : Math.abs(a.r.y + Math.trunc(a.r.h / 2) - cy);
            const b2 = vertical ? Math.abs(b.r.x + Math.trunc(b.r.w / 2) - cx) : Math.abs(b.r.y + Math.trunc(b.r.h / 2) - cy);
            if (reverse)
                return a2 < b2 ? 1 : a2 === b2 ? 0 : -1;
            return a2 < b2 ? -1 : a2 === b2 ? 0 : 1;
        });
        if (reverse)
            list.reverse();
        let seen = false;
        for (const entry of list) {
            if (entry.item === from) {
                seen = true;
                continue;
            }
            if (seen) {
                entry.item.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }
    // Arrow keys on a control that does not take them.
    function navigate(from, event) {
        if (window.windowsKeys) {
            // wxWindowMSW::MSWProcessMessage: Ctrl+arrow is not navigation.
            if (event.modifiers & Qt.ControlModifier)
                return;
            const list = focusables();
            const at = list.indexOf(from);
            if (at < 0 || list.length === 0)
                return;
            const back = event.key === Qt.Key_Up || event.key === Qt.Key_Left;
            list[(at + (back ? -1 : 1) + list.length) % list.length].forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        // GtkWindow's move-focus bindings: the arrows alone or with Ctrl.
        if ((event.modifiers & ~(Qt.KeypadModifier | Qt.ControlModifier)) === Qt.NoModifier)
            gtkDirectional(from, event.key);
    }
    function isArrow(key) {
        return key === Qt.Key_Up || key === Qt.Key_Down || key === Qt.Key_Left || key === Qt.Key_Right;
    }
    // Return, Escape and Space on a control that takes no keys of its own
    // (checkbox, dropdown): OnCharHook ends the dialog for Return and Escape.
    function plainControlKey(item, event, arrows) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Escape) {
            event.accepted = true;
            if (window.unmodified(event))
                window.endWithoutButton();
        } else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            event.accepted = true; // not Return to OnCharHook, no handler
        } else if (arrows && isArrow(event.key)) {
            event.accepted = true;
            window.navigate(item, event);
        } else {
            event.accepted = false;
        }
    }
    // A text field: Return and Escape go to the field, which ignores them
    // (single line) or takes Return as a new line (the textbox).
    function textFieldKey(event, multiline) {
        if (event.key === Qt.Key_Escape) {
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // HikariTextCtrl's Return accelerator: multiline only; keypad
            // Enter matches it on Windows only.
            const newLine = multiline && (event.key === Qt.Key_Return || window.windowsKeys);
            event.accepted = !newLine;
        } else {
            event.accepted = false;
        }
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
                id: buttonRepeater
                model: window.controller.buttons
                delegate: Button {
                    id: dialogButton
                    required property string modelData
                    required property int index
                    objectName: "dialogButton" + index
                    text: modelData
                    onClicked: window.press(index)
                    // MappedButton: Return (and on Windows keypad Enter) is
                    // its accelerator; Escape reaches OnCharHook; Space and the
                    // rest are not handled.
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            event.accepted = true;
                            if (window.unmodified(event) && (event.key === Qt.Key_Return || window.windowsKeys))
                                window.press(index);
                        } else if (event.key === Qt.Key_Escape) {
                            event.accepted = true;
                            if (window.unmodified(event))
                                window.endWithoutButton();
                        } else if (event.key === Qt.Key_Space) {
                            event.accepted = true;
                        } else if (window.isArrow(event.key)) {
                            event.accepted = true;
                            window.navigate(dialogButton, event);
                        } else {
                            event.accepted = false;
                        }
                    }
                    Keys.onReleased: function(event) {
                        event.accepted = event.key === Qt.Key_Space; // no click on release either
                    }
                }
            }
        }
    }

    Component {
        id: labelControl
        Label {
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: null
            text: spec.label
            function readValue() { return undefined; }
        }
    }
    Component {
        id: editControl
        TextField {
            id: edit
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: edit
            text: spec.text
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            Keys.onPressed: function(event) { window.textFieldKey(event, false); }
            function readValue() { return text; }
        }
    }
    Component {
        id: textboxControl
        TextArea {
            id: textbox
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: textbox
            text: spec.text
            implicitHeight: Math.max(30, contentHeight + topPadding + bottomPadding)
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            Keys.onPressed: function(event) { window.textFieldKey(event, true); }
            function readValue() { return text; }
        }
    }
    Component {
        id: intControl
        // Legacy NumCtrl (the int constructor): a text field taking digits,
        // and '-' when the range goes below zero, at most 10 characters; no
        // key steps it (Up and Down move the caret). The mouse wheel adds a
        // notch at a time, and dragging with the right button held changes it
        // by 1 per 8 pixels up or down and by 10 per 10 pixels sideways.
        TextField {
            id: intField
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: intField
            // NumCtrl::value: the last number parsed or stepped to.
            property real value: Math.min(Math.max(spec.intValue, spec.intMin), spec.intMax)
            property bool holding: false
            property real oldX: 0
            property real oldY: 0
            Component.onCompleted: text = String(value) // shown once; the value may leave the range
            maximumLength: 10
            validator: RegularExpressionValidator {
                regularExpression: intField.spec.intMin < 0 ? /^[0-9-]*$/ : /^[0-9]*$/
            }
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            Keys.onPressed: function(event) { window.textFieldKey(event, false); }
            // NumCtrl::OnNumWrite: a typed number becomes the value.
            onTextEdited: {
                const n = Number(text.replace(",", "."));
                if (text !== "" && text !== "-" && !isNaN(n))
                    value = n;
            }
            function show(n) { text = String(n); }
            function readValue() { return text; }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                preventStealing: true
                onPressed: function(mouse) {
                    intField.holding = true;
                    intField.oldX = mouse.x;
                    intField.oldY = mouse.y;
                    intField.forceActiveFocus(Qt.MouseFocusReason);
                }
                onReleased: {
                    intField.holding = false;
                    intField.forceActiveFocus(Qt.MouseFocusReason);
                }
                onPositionChanged: function(mouse) {
                    if (!intField.holding)
                        return;
                    const f = intField;
                    const absy = Math.abs(Math.trunc(mouse.y) - Math.trunc(f.oldY));
                    const absx = Math.abs(Math.trunc(mouse.x) - Math.trunc(f.oldX));
                    if (absy >= absx) {
                        if (absy >= 8) {
                            const n = f.oldY < mouse.y ? f.value - 1 : f.value + 1;
                            if (n < f.spec.intMin || n > f.spec.intMax)
                                return;
                            f.show(n);
                            f.oldY = mouse.y;
                            f.oldX = mouse.x;
                            f.value = n;
                        }
                    } else if (absx >= 10) {
                        let n = f.oldX > mouse.x ? f.value - 10 : f.value + 10;
                        if ((f.value === f.spec.intMin && n < f.spec.intMin) || (f.value === f.spec.intMax && n > f.spec.intMax))
                            return;
                        n = Math.min(Math.max(n, f.spec.intMin), f.spec.intMax);
                        f.show(n);
                        f.oldX = mouse.x;
                        f.oldY = mouse.y;
                        f.value = n;
                    }
                }
                onWheel: function(wheel) {
                    // value += rotation / delta; outside the range nothing is
                    // shown, but the value keeps the step.
                    intField.value += Math.trunc(wheel.angleDelta.y / 120);
                    if (intField.value < intField.spec.intMin || intField.value > intField.spec.intMax)
                        return;
                    intField.show(intField.value);
                }
            }
        }
    }
    Component {
        id: floatControl
        TextField {
            id: floatField
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: floatField
            text: String(spec.number)
            // As legacy NumCtrl: digits with a point or comma; no step (legacy
            // ignores it; its float edit has no wheel or drag either); the
            // controller clamps to the range.
            validator: RegularExpressionValidator { regularExpression: /^-?[0-9]*([.,][0-9]*)?$/ }
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            Keys.onPressed: function(event) { window.textFieldKey(event, false); }
            function readValue() { return text; }
        }
    }
    Component {
        id: dropdownControl
        ComboBox {
            id: dropdown
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: dropdown
            model: spec.items
            currentIndex: spec.items.indexOf(spec.text)
            Accessible.name: spec.hint || spec.name
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            // HikariChoice: Up/Down are its accelerators (previous/next
            // choice); Left/Right are dialog navigation.
            Keys.onPressed: function(event) {
                window.plainControlKey(dropdown, event, false);
                if (!event.accepted && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
                    event.accepted = true;
                    window.navigate(dropdown, event);
                }
            }
            Keys.onReleased: function(event) { event.accepted = event.key === Qt.Key_Space; }
            function readValue() { return currentIndex >= 0 ? currentText : spec.text; }
        }
    }
    Component {
        id: colorControl
        RowLayout {
            id: colorRow
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: pickButton
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
                id: pickButton
                objectName: "colorPick"
                text: "…"
                ToolTip.text: colorRow.spec.hint
                ToolTip.visible: hovered && colorRow.spec.hint !== ""
                onClicked: picker.open()
                // ButtonColorPicker (a MappedButton): its Return accelerator
                // is not handled on Windows; wxGTK retries it as a click.
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        event.accepted = true;
                        if (event.key === Qt.Key_Return && window.unmodified(event) && !window.windowsKeys)
                            picker.open();
                    } else if (event.key === Qt.Key_Escape) {
                        event.accepted = true;
                        if (window.unmodified(event))
                            window.endWithoutButton();
                    } else if (event.key === Qt.Key_Space) {
                        event.accepted = true;
                    } else if (window.isArrow(event.key)) {
                        event.accepted = true;
                        window.navigate(pickButton, event);
                    } else {
                        event.accepted = false;
                    }
                }
                Keys.onReleased: function(event) { event.accepted = event.key === Qt.Key_Space; }
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
            id: check
            readonly property var spec: parent ? parent.spec : ({})
            readonly property Item focusTarget: check
            text: spec.label
            checked: spec.checked
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            // HikariCheckBox handles no keys: Space does not toggle it.
            Keys.onPressed: function(event) { window.plainControlKey(check, event, true); }
            Keys.onReleased: function(event) { event.accepted = event.key === Qt.Key_Space; }
            function readValue() { return checked; }
        }
    }
}
