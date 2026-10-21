pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
// PK1 (#301): the list chooser, one for the three 220 x 160 list dialogs
// (legacy HikariListBox: the audio track, the MKV subtitle track and the
// spelling suggestions). By the small forms rule a popover at the control
// or text that asked for it (a misspelled word), else a compact window on
// DG1's frame (a file with several tracks, opened from a menu or a drop).
// The first row is chosen at the start; the arrows choose, Enter takes
// the row, and so does a click in the popover or a double click in the
// window; Esc, Cancel or a press outside
// the popover take none.
//
// choose(options, done): title, text (the window's main line), rows
// (strings, or {text, detail}), current, acceptText (the window's button),
// anchor (an item: the popover under it), at (a rectangle in the
// anchor: the popover under that part of it, a word); done(row), done(-1) when none was taken.
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
    readonly property bool opened: popover.visible || compact.visible
    readonly property bool inPopover: popover.visible
    readonly property alias compactWindow: compact
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
            compact.open()
            compactList.focusList()
        }
    }
    function take(row) {
        const callback = done
        done = null
        if (popover.visible)
            popover.keep()
        if (compact.visible)
            compact.accept()
        if (callback)
            callback(row)
    }
    function none(closing) {
        const callback = done
        done = null
        if (popover.visible)
            popover.keep()
        if (compact.visible && closing !== true)
            compact.close()
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
        // A press outside takes nothing.
        onKept: if (chooser.done !== null) chooser.none()
        ChoiceList { id: popoverList; host: chooser; Layout.preferredWidth: 200 }
    }
    DialogWindow {
        id: compact
        objectName: "listChooserWindow"
        dialogId: "listChooser"
        settings: chooser.settings
        modal: true
        title: chooser.title
        defaultButton: takeButton
        onRejected: chooser.none(true)
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

    component ChoiceList: Rectangle {
        id: frame
        required property Item host
        property bool framed: false
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
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            onCurrentIndexChanged: frame.host.current = currentIndex
            Keys.onReturnPressed: frame.host.take(currentIndex)
            Keys.onEnterPressed: frame.host.take(currentIndex)
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: "listChooserRow" + index
                width: ListView.view.width
                implicitHeight: 28
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: frame.host.textOf(modelData)
                Accessible.description: frame.host.detailOf(modelData)
                // In the popover a click takes the row; in the window it chooses it.
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
