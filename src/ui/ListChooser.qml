pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: chooser
    property SettingsStore settings: null
    property string title: ""
    property string text: ""
    property var rows: []
    property int current: 0
    property string acceptText: ""
    property var done: null
    readonly property bool opened: popover.visible || (compactWindow !== null && compactWindow.visible)
    readonly property bool inPopover: popover.visible
    readonly property DialogWindow compactWindow: compactLoader.item
    visible: false

    function choose(options, callback) {
        const o = options || {}
        title = o.title || ""
        text = o.text || ""
        rows = o.rows || []
        current = o.current !== undefined ? o.current : (rows.length > 0 ? 0 : -1)
        acceptText = o.acceptText || qsTr("OK")
        done = callback || null
        if (o.anchor && o.anchor.visible) {
            popover.openAt(o.anchor, null, o.at)
            popoverList.focusList()
        } else {
            compactLoader.active = true
            compactWindow.open()
            compactWindow.focusList()
        }
    }
    function take(row) {
        const callback = done
        done = null
        if (popover.visible)
            popover.keep()
        if (compactWindow && compactWindow.visible)
            compactWindow.accept()
        if (callback)
            callback(row)
    }
    function none(closing) {
        const callback = done
        done = null
        if (popover.visible)
            popover.keep()
        if (compactWindow && compactWindow.visible && closing !== true)
            compactWindow.close()
        if (callback)
            callback(-1)
    }
    function textOf(r) { return typeof r === "string" ? r : r.text }
    function detailOf(r) { return typeof r === "string" ? "" : (r.detail || "") }

    Popover {
        id: popover
        objectName: "listChooserPopover"
        title: chooser.title
        padding: 4
        onReverted: chooser.none()
        onKept: if (chooser.done !== null) chooser.none()
        ChoiceList { id: popoverList; host: chooser; Layout.preferredWidth: 200 }
    }
    Loader {
        id: compactLoader
        active: false
        sourceComponent: compactComponent
    }
    Component {
        id: compactComponent
        DialogWindow {
            id: compact
            objectName: "listChooserWindow"
            dialogId: "listChooser"
            settings: chooser.settings
            modal: true
            title: chooser.title
            defaultButton: takeButton
            onRejected: chooser.none(true)
            function focusList() { compactList.focusList() }
            Label {
                objectName: "listChooserText"
                visible: chooser.text.length > 0
                text: chooser.text
                font: compact.fontAt(14, Font.DemiBold)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.topMargin: 4
            }
            ChoiceList { id: compactList; host: chooser; framed: true; Layout.preferredWidth: 352 }
            buttons: [
                PrimaryButton {
                    id: takeButton
                    objectName: "listChooserTake"
                    text: chooser.acceptText
                    enabled: chooser.current >= 0
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: chooser.take(chooser.current)
                },
                Button {
                    objectName: "listChooserCancel"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: chooser.none()
                }
            ]
        }
    }

    component ChoiceList: Rectangle {
        id: frame
        required property Item host
        property bool framed: false
        // ListView's contentHeight settles a frame late, so the row count decides whether it scrolls.
        readonly property bool scrolls: list.count > 8
        color: framed ? Theme.field : "transparent"
        border.width: framed ? 1 : 0
        border.color: list.activeFocus && framed ? Theme.focus : Theme.line
        implicitHeight: Math.min(list.contentHeight, 8 * 28) + (framed ? 6 : 0)
        Layout.fillWidth: true
        function focusList() { list.forceActiveFocus() }
        ListView {
            id: list
            objectName: "listChooserList"
            anchors.fill: parent
            anchors.margins: frame.framed ? 3 : 0
            clip: true
            model: frame.host.rows
            currentIndex: frame.host.current
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: frame.host.title
            ScrollBar.vertical: ScrollBar { objectName: "listChooserScrollBar"; policy: frame.scrolls ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            onCurrentIndexChanged: frame.host.current = currentIndex
            Keys.onReturnPressed: frame.host.take(currentIndex)
            Keys.onEnterPressed: frame.host.take(currentIndex)
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: "listChooserRow" + index
                width: ListView.view.width - (frame.scrolls ? ListView.view.ScrollBar.vertical.width : 0)
                implicitHeight: 28
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: frame.host.textOf(modelData)
                Accessible.description: frame.host.detailOf(modelData)
                onClicked: {
                    if (frame.host.inPopover)
                        frame.host.take(index)
                    else
                        frame.host.current = index
                }
                onDoubleClicked: frame.host.take(index)
                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        text: frame.host.textOf(row.modelData)
                        color: row.highlighted ? row.palette.highlightedText : row.palette.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: text.length > 0
                        text: frame.host.detailOf(row.modelData)
                        color: row.highlighted ? row.palette.highlightedText : Theme.muted
                    }
                }
            }
        }
    }
}
