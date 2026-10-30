// Fusion's outline (the window colour darkened) all but vanishes on black panels.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI
import QtQuick.Templates as T

F.ComboBox {
    id: control
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
        contentItem: Text {
            text: choice.text
            // File text as markup would fetch an <img> (and Qt's IconLabel crashes laying one out).
            textFormat: Text.PlainText
            font: choice.font
            color: choice.down || choice.highlighted ? F.Fusion.highlightedText(choice.palette) : choice.palette.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: FI.ButtonPanel {
        implicitWidth: 120
        implicitHeight: 24
        control: control
        visible: !control.flat || control.down
        highlighted: control.contentItem.activeFocus
        border.color: !control.enabled ? control.palette.mid
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : (StyleColours.themed ? StyleColours.control : control.palette.mid)
        FocusFrame { control: control; shown: control.visualFocus }
    }
    // Fusion's inner text field is the box's Edit to UI Automation; it needs the box's name.
    Binding {
        target: control.contentItem
        property: "Accessible.name"
        value: control.Accessible.name
    }
    Binding {
        target: control.popup
        property: "closePolicy"
        value: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside | T.Popup.CloseMultiple
    }
    Binding {
        target: control.popup ? control.popup.background : null
        property: "border.color"
        value: control.palette.mid
    }
}
