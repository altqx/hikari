import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Templates as T

F.ComboBox {
    id: control
    leftPadding: control.mirrored ? 26 : 8
    rightPadding: control.mirrored ? 8 : 26
    delegate: F.MenuItem {
        id: choice
        required property var model
        required property var modelData
        required property int index

        width: ListView.view.width
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
        font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        implicitHeight: 24
        background: Rectangle {
            visible: StyleColours.themed ? choice.highlighted || choice.down : false
            color: StyleColours.select
            StateOverlay { control: choice; pressed: choice.down; visible: StyleColours.themed && choice.down }
        }
        contentItem: Text {
            text: choice.text
            // File text as markup would fetch an <img> (and Qt's IconLabel crashes laying one out).
            textFormat: Text.PlainText
            font: choice.font
            color: !StyleColours.themed ? (choice.down || choice.highlighted ? F.Fusion.highlightedText(choice.palette) : choice.palette.text)
                : StyleColours.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    indicator: Chevron {
        objectName: "comboChevron"
        x: control.mirrored ? 8 : control.width - width - 8
        y: (control.height - height) / 2
        width: 10
        height: 5
        color: !StyleColours.themed ? control.palette.buttonText
            : control.enabled ? StyleColours.muted : StyleColours.disabled
    }
    background: FieldBackground {
        control: control
        visible: !control.flat || control.down
        open: control.popup.visible
        editing: control.editable && control.activeFocus
        color: !StyleColours.themed ? control.palette.button
            : control.editable ? StyleColours.field : StyleColours.raised
        keyboardFocus: control.visualFocus
        StateOverlay { control: control; pressed: control.down; visible: !control.editable && StyleColours.themed && control.enabled && (control.down || control.hovered) }
    }
    // Fusion's inner text field is the box's Edit to UI Automation; it needs the box's name.
    Binding {
        target: control.contentItem
        property: "Accessible.name"
        value: control.Accessible.name
    }
    // Qt gives an editable box's field focus only on Tab or a shortcut, and Fusion makes it read-only while the list is open (#801).
    Connections {
        target: control.Window
        function onActiveFocusItemChanged() {
            if (control.editable && control.Window.activeFocusItem === control)
                control.contentItem.forceActiveFocus(Qt.MouseFocusReason)
        }
    }
    Binding {
        target: control.contentItem
        property: "readOnly"
        value: false
        when: control.editable
    }
    Connections {
        target: control.editable ? control.contentItem : null
        ignoreUnknownSignals: true
        function onTextEdited() { control.popup.close() }
    }
    Connections {
        target: TextMenus.adopt(control.contentItem)
        function onRequested(position) {
            TextMenus.menuFor(control.contentItem)?.popup(control.contentItem, position)
        }
    }
    Binding {
        target: control.popup
        property: "closePolicy"
        value: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside | T.Popup.CloseMultiple
    }
    Binding {
        target: control.popup ? control.popup.background : null
        property: "border.color"
        value: StyleColours.themed ? StyleColours.line : control.palette.mid
    }
    Binding {
        target: control.popup ? control.popup.background : null
        property: "radius"
        value: 6
    }
}
