// E6: the Line editor's tag list (legacy PopupTagList/PopupWindow,
// TextEditorTagList.cpp at 20d647c4). It opens below the caret's line while
// an override tag is typed and never takes keyboard focus, so the text field
// keeps its caret and any input-method composition; the field routes Up,
// Down, Enter and Escape to it (RoleField in Main.qml). Ten rows at most,
// with a scroll bar beyond; the pointer selects the row under it, a left
// click puts it, the wheel scrolls three rows, a right click opens the
// options menu. Colours are the theme palette's (legacy MENUBAR_BACKGROUND,
// WINDOW_BORDER, MENU_BACKGROUND_SELECTION, MENU_BORDER_SELECTION and
// WINDOW_TEXT).
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Popup {
    id: popup
    required property TagListController controller
    required property TextArea field
    // Enter or a click chose the selected row.
    signal chosen()

    readonly property real rowHeight: controller.rowHeight(field.font)
    readonly property int shownRows: Math.min(controller.rows.length, controller.maxVisible)
    readonly property bool menuOpen: optionsMenu.visible

    objectName: field.objectName + "TagList"
    parent: field
    // TextEditor::OnCharPress: x after the caret's text on its line, y the
    // line's bottom plus 5 (DialogueTextEditor.cpp:451-472).
    x: field.cursorRectangle.x
    y: field.cursorRectangle.y + field.cursorRectangle.height + 5
    width: controller.popupWidth(field.font)
    height: rowHeight * shownRows + 2
    padding: 1
    focus: false
    modal: false
    closePolicy: Popup.NoAutoClose
    visible: controller.popupShown && (field.activeFocus || menuOpen)

    background: Rectangle {
        color: popup.palette.window
        border.color: popup.palette.mid
    }

    contentItem: Item {
        clip: true
        ListView {
            id: rows
            objectName: popup.objectName + "Rows"
            anchors.fill: parent
            anchors.rightMargin: scrollBar.visible ? scrollBar.width + 1 : 0
            model: popup.controller.rows
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            contentY: popup.controller.scrollPosition * popup.rowHeight
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Tag list")
            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                readonly property bool selected: index === popup.controller.selection
                width: ListView.view.width
                height: popup.rowHeight
                color: "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData
                Accessible.selected: selected
                Accessible.selectable: true
                // The selected row: a rectangle inset by 2 (OnPaint).
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: row.selected
                    color: popup.palette.highlight
                    border.color: popup.palette.highlight.darker(1.3)
                }
                Text {
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData
                    font: popup.field.font
                    color: row.selected ? popup.palette.highlightedText : popup.palette.windowText
                    textFormat: Text.PlainText
                }
            }
        }
        ScrollBar {
            id: scrollBar
            objectName: popup.objectName + "ScrollBar"
            orientation: Qt.Vertical
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: popup.controller.rows.length > popup.controller.maxVisible
            size: popup.controller.maxVisible / Math.max(1, popup.controller.rows.length)
            position: popup.controller.scrollPosition / Math.max(1, popup.controller.rows.length)
            policy: ScrollBar.AlwaysOn
            // OnScroll: the dragged thumb sets the first row.
            onPositionChanged: if (pressed)
                popup.controller.scrollBy(Math.round(position * popup.controller.rows.length)
                                          - popup.controller.scrollPosition)
        }
        MouseArea {
            objectName: popup.objectName + "Pointer"
            anchors.fill: rows
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            function rowAt(y) { return Math.floor(y / popup.rowHeight) }
            onPositionChanged: mouse => popup.controller.pointerAt(rowAt(mouse.y))
            onReleased: mouse => {
                if (!popup.controller.pointerAt(rowAt(mouse.y)))
                    return
                if (mouse.button === Qt.LeftButton)
                    popup.chosen()
                else
                    optionsMenu.popup()
            }
            onWheel: wheel => popup.controller.wheel(Math.round(wheel.angleDelta.y / 120))
        }
    }

    // The right-click menu (TextEditorTagList.cpp:131-149).
    ShellMenu {
        id: optionsMenu
        objectName: popup.objectName + "Options"
        ShellMenuItem {
            objectName: popup.objectName + "ShowDescription"
            text: qsTr("Show description")
            checkable: true
            checked: (popup.controller.options & 4) !== 0
            onTriggered: popup.controller.toggleOption(4)
        }
        ShellMenuItem {
            objectName: popup.objectName + "ShowAllTags"
            text: qsTr("Show all tags")
            checkable: true
            checked: (popup.controller.options & 1) !== 0
            onTriggered: popup.controller.toggleOption(1)
        }
        ShellMenuItem {
            objectName: popup.objectName + "ShowVsfilterModTags"
            text: qsTr("Show VSFiltermod tags")
            checkable: true
            checked: (popup.controller.options & 2) !== 0
            onTriggered: popup.controller.toggleOption(2)
        }
        onClosed: if (!popup.field.activeFocus) popup.field.forceActiveFocus()
    }

    // Announced while the field keeps focus: the list on opening, then each
    // selected row (assistive technology does not follow the popup itself).
    Connections {
        target: popup.controller
        function onChanged() {
            const said = popup.controller.selectedAnnouncement
            if (popup.visible && said.length > 0 && said !== popup.lastAnnouncement) {
                popup.lastAnnouncement = said
                popup.field.Accessible.announce(said)
            }
            if (!popup.controller.open)
                popup.lastAnnouncement = ""
        }
    }
    property string lastAnnouncement: ""
}
