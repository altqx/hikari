import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: row
    required property int index
    required property string kind
    required property string itemId
    required property string label
    required property string labelMarkup
    required property string description
    required property string descriptionMarkup
    required property string detail
    required property string icon
    required property string shortcut
    required property string trailing
    required property string disabledReason
    required property string tooltip
    required property bool picked
    required property bool firstHeading
    required property var item
    property bool current: false
    property bool many: false
    readonly property bool heading: kind === "heading"
    readonly property bool unavailable: disabledReason.length > 0
    signal clicked()
    signal toggled()
    signal buttonPressed(string button)
    readonly property bool marked: !many && !heading && item?.checkable === true
    readonly property var itemButtons: current && item && item.buttons ? item.buttons : []

    objectName: heading ? "quickPickHeading" + index : "quickPickRow_" + itemId
    height: heading ? 20 : detail.length > 0 ? 40 : 24
    Accessible.role: heading ? Accessible.StaticText : Accessible.ListItem
    Accessible.ignored: heading
    Accessible.name: label
    Accessible.description: [unavailable ? "" : description, shortcut,
                             unavailable ? qsTranslate("HikariSub.QuickPick", "unavailable: %1").arg(disabledReason) : "",
                             many ? (picked ? qsTranslate("HikariSub.QuickPick", "picked")
                                            : qsTranslate("HikariSub.QuickPick", "not picked")) : "",
                             tooltip].filter(t => t.length > 0).join(", ")
    Accessible.selected: current
    Accessible.checkable: marked
    Accessible.checked: marked && item?.checked === true
    Accessible.selectable: !heading

    Rectangle {
        visible: row.heading && !row.firstHeading
        width: parent.width
        height: 1
        color: Theme.line
    }
    Text {
        visible: row.heading
        anchors.fill: parent
        anchors.leftMargin: 6
        verticalAlignment: Text.AlignVCenter
        text: row.label
        textFormat: Text.PlainText
        font.pixelSize: 12
        color: Theme.muted
        elide: Text.ElideRight
    }

    Rectangle {
        id: fill
        visible: !row.heading
        anchors.fill: parent
        radius: 4
        color: row.current ? Theme.select : mouse.containsMouse ? Theme.raised : "transparent"
        border.width: row.current && Theme.highContrast ? 1 : 0
        border.color: Theme.accent
    }

    Item {
        id: gutter
        visible: !row.heading
        width: 24
        height: 24
        CheckBox {
            objectName: "quickPickCheck_" + row.itemId
            visible: row.many
            anchors.centerIn: parent
            checked: row.picked
            enabled: !row.unavailable
            focusPolicy: Qt.NoFocus
            onClicked: row.toggled()
        }
        CheckBox {
            objectName: "quickPickMark_" + row.itemId
            visible: row.marked
            anchors.centerIn: parent
            checked: row.item?.checked === true
            checkable: false
            enabled: !row.unavailable
            focusPolicy: Qt.NoFocus
        }
        Icon {
            visible: !row.many && !row.marked && row.icon.length > 0
            anchors.centerIn: parent
            iconRole: row.icon
            size: 16
            enabled: !row.unavailable
        }
    }

    Item {
        visible: !row.heading
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.rightMargin: 6
        height: 24
        Row {
            id: itemButtonRow
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: row.itemButtons
                IconToolButton {
                    required property var modelData
                    objectName: "quickPickItemButton_" + row.itemId + "_" + modelData.id
                    width: 22
                    height: 22
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    iconRole: modelData.icon
                    text: modelData.tooltip
                    onClicked: row.buttonPressed(modelData.id)
                }
            }
        }
        Text {
            id: end
            anchors.right: itemButtonRow.left
            anchors.rightMargin: itemButtonRow.width > 0 ? 6 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: [row.trailing, row.shortcut].filter(t => t.length > 0).join("   ")
            textFormat: Text.PlainText
            color: Theme.muted
        }
        Text {
            id: name
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width - end.width - itemButtonRow.width - 18)
            text: row.labelMarkup
            textFormat: Text.StyledText
            elide: Text.ElideRight
            color: row.unavailable ? Theme.disabled : Theme.text
        }
        Text {
            anchors.left: name.right
            anchors.leftMargin: 12
            anchors.right: end.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: row.unavailable ? row.disabledReason : row.descriptionMarkup
            textFormat: row.unavailable ? Text.PlainText : Text.StyledText
            elide: Text.ElideRight
            color: Theme.muted
        }
    }
    Text {
        visible: !row.heading && row.detail.length > 0
        x: gutter.width
        y: 20
        width: parent.width - gutter.width - 6
        text: row.detail
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.pixelSize: 12
        color: Theme.muted
    }

    MouseArea {
        id: mouse
        visible: !row.heading
        anchors.fill: parent
        hoverEnabled: true
        onClicked: row.many ? row.toggled() : row.clicked()
    }
    ToolTip.visible: tooltip.length > 0 && mouse.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 600
}
