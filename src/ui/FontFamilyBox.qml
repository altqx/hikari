pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

TextField {
    id: box
    property var families: []
    property string rowName: "fontFamilyRow"
    readonly property alias popup: popup
    readonly property alias shown: filter.matches
    readonly property alias highlightedIndex: popup.highlighted
    readonly property string browsedFamily: popup.visible && popup.highlighted >= 0 ? popup.highlightedFamily : ""
    signal picked(string family)

    function openAll() {
        filter.text = ""
        popup.open()
        popup.highlight(filter.row(text))
    }
    function toggle() {
        forceActiveFocus(Qt.MouseFocusReason)
        if (popup.visible)
            popup.close()
        else
            openAll()
    }
    function step(by) {
        if (!popup.visible) {
            openAll()
            return
        }
        if (filter.count > 0)
            popup.highlight(Math.max(0, Math.min(filter.count - 1, popup.highlighted + by)))
    }
    function pick(family) {
        text = family
        popup.typed = false
        popup.close()
        picked(family)
    }
    function choose() {
        pick(popup.visible && popup.highlighted >= 0 ? popup.highlightedFamily : text)
    }

    rightPadding: arrow.width + 2
    Connections {
        target: box
        function onTextEdited() {
            popup.typed = true
            filter.text = box.text
            popup.open()
            popup.highlight(filter.count > 0 ? 0 : -1)
            list.positionViewAtBeginning()
        }
        function onActiveFocusChanged() {
            if (box.activeFocus)
                return
            popup.close()
            filter.text = ""
            const at = filter.row(box.text)
            if (popup.typed && at >= 0)
                box.pick(filter.at(at))
        }
    }
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)
    Keys.onReturnPressed: choose()
    Keys.onEnterPressed: choose()
    Keys.onEscapePressed: event => {
        event.accepted = popup.visible
        popup.close()
    }

    IconToolButton {
        id: arrow
        objectName: "fontFamilyBoxArrow"
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 2
        width: height
        iconRole: "chevron-down"
        iconSize: 12
        text: qsTr("Show fonts")
        focusPolicy: Qt.NoFocus
        down: popup.visible
        onClicked: box.toggle()
    }

    FontFamilyFilter {
        id: filter
        families: box.families
    }

    Popup {
        id: popup
        objectName: "fontFamilyBoxPopup"
        property int highlighted: -1
        property string highlightedFamily: ""
        property bool typed: false
        property bool below: true
        property real room: 200
        readonly property real rowHeight: 28
        function highlight(row) {
            highlighted = row
            highlightedFamily = row >= 0 ? filter.at(row) : ""
            if (row >= 0)
                list.positionViewAtIndex(row, ListView.Contain)
        }
        function place() {
            const win = box.Window.window
            if (!win)
                return
            const top = box.mapToItem(null, 0, 0).y
            const under = win.height - top - box.height - 6
            const over = top - 6
            below = under >= 160 || under >= over
            room = Math.max(rowHeight + 2, below ? under : over)
        }
        y: below ? box.height : -height
        width: box.width
        height: Math.min(Math.max(filter.count, 1) * rowHeight + 2, 12 * rowHeight + 2, room)
        padding: 1
        focus: false
        closePolicy: Popup.CloseOnPressOutsideParent
        onAboutToShow: place()
        onOpened: list.positionViewAtIndex(Math.max(0, highlighted), ListView.Center)
        onClosed: highlight(-1)

        Connections {
            target: filter
            function onModelReset() { popup.highlightedFamily = popup.highlighted >= 0 ? filter.at(popup.highlighted) : "" }
        }

        background: Rectangle {
            color: Theme.field
            border.color: Theme.line
        }
        contentItem: Item {
            Label {
                objectName: "fontFamilyBoxNoMatch"
                visible: filter.count === 0
                text: qsTr("No installed font matches")
                textFormat: Text.PlainText
                color: Theme.muted
                elide: Text.ElideRight
                width: parent.width
                height: popup.rowHeight
                leftPadding: 8
                rightPadding: 8
                verticalAlignment: Text.AlignVCenter
            }
            ListView {
                id: list
                objectName: "fontFamilyBoxList"
                anchors.fill: parent
                clip: true
                model: filter
                currentIndex: popup.highlighted
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: box.Accessible.name
                ScrollBar.vertical: ScrollBar {}
                delegate: FontFamilyRow {
                    required property int index
                    objectName: box.rowName
                    width: ListView.view.width
                    highlighted: index === popup.highlighted
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: box.hoverEnabled
                    onHoveredChanged: if (hovered) popup.highlight(index)
                    onClicked: box.pick(family)
                }
            }
        }
    }
}
