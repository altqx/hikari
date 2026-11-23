pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popover {
    id: picker
    objectName: "stylePicker"
    title: qsTr("Choose a style")
    initialFocus: filter
    required property var styles
    required property int shellWindow
    required property var app
    required property var editor
    property int activeNumber: 0
    property var rows: []
    readonly property var shown: {
        const text = filter.text.trim().toLocaleLowerCase()
        return text.length === 0 ? rows : rows.filter(r => r.name.toLocaleLowerCase().includes(text))
    }
    readonly property var highlighted: shown[list.currentIndex] ?? null
    readonly property StyleStepper stepper: StyleStepper {
        app: picker.app
        editor: picker.editor
    }

    function openFor(anchorItem) {
        reload()
        filter.text = ""
        highlight(editor.style)
        openAt(anchorItem)
    }
    function reload() {
        rows = styles.panelRows(shellWindow)
    }
    function highlight(name) {
        list.currentIndex = Math.max(0, shown.findIndex(r => r.name === name))
    }
    function choose() {
        const row = highlighted
        if (!row)
            return
        if (!stepper.on) {
            stepper.apply(row.name)
            keep()
        } else if (stepper.apply(row.name)) {
            filter.text = ""
            highlight(row.name)
        }
    }
    onShownChanged: if (list.currentIndex >= shown.length || list.currentIndex < 0) list.currentIndex = shown.length > 0 ? 0 : -1
    Connections {
        target: picker.styles
        enabled: picker.visible
        function onChanged() { picker.reload() }
        function onDocumentChanged() { picker.reload() }
    }

    RowLayout {
        spacing: 4
        Layout.fillWidth: true
        TextField {
            id: filter
            objectName: "stylePickerFilter"
            Layout.fillWidth: true
            Layout.preferredWidth: 248
            placeholderText: qsTr("Filter")
            Accessible.name: qsTr("Filter styles")
            Keys.onPressed: event => {
                const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                    const step = event.key === Qt.Key_Down ? 1 : -1
                    list.currentIndex = Math.max(0, Math.min(picker.shown.length - 1, list.currentIndex + step))
                    event.accepted = true
                } else if (enter && picker.stepper.on && (event.modifiers & Qt.ShiftModifier)) {
                    picker.stepper.back()
                    event.accepted = true
                } else if (enter) {
                    picker.choose()
                    event.accepted = true
                }
            }
        }
        IconToolButton {
            objectName: "stylePickerStep"
            iconRole: "style-apply"
            text: qsTr("Step through lines")
            tip: qsTr("Step through lines: Enter applies the style and goes to the next line, Shift+Enter goes back")
            checkable: true
            checked: picker.stepper.on
            focusPolicy: Qt.TabFocus
            onToggled: picker.stepper.on = checked
        }
    }
    Label {
        objectName: "stylePickerEmpty"
        visible: picker.shown.length === 0
        Layout.fillWidth: true
        color: Theme.muted
        text: picker.rows.length === 0 ? qsTr("No styles") : qsTr("No matching styles")
    }
    ListView {
        id: list
        objectName: "stylePickerList"
        visible: count > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(240, Math.max(contentHeight, count > 0 ? 32 : 0))
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        model: picker.shown
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Styles")
        ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            objectName: "stylePickerRow" + index
            width: ListView.view.width
            highlighted: ListView.isCurrentItem
            focusPolicy: Qt.NoFocus
            Accessible.name: modelData.name + ", " + modelData.font + " " + modelData.size
            onClicked: {
                list.currentIndex = index
                picker.choose()
            }
            contentItem: RowLayout {
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: row.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.weight: row.modelData.name === picker.editor.style ? Font.DemiBold : Font.Normal
                }
                Label {
                    text: row.modelData.font + " · " + row.modelData.size
                    textFormat: Text.PlainText
                    color: Theme.muted
                    font.features: { "tnum": 1 }
                }
            }
        }
    }
    Label {
        objectName: "stylePickerFoot"
        visible: picker.stepper.on && text.length > 0
        Layout.fillWidth: true
        Layout.maximumWidth: 300
        wrapMode: Text.Wrap
        color: Theme.muted
        text: picker.stepper.foot(picker.highlighted?.name ?? "", picker.activeNumber)
    }
}
