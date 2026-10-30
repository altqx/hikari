pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popover {
    id: chooser
    objectName: "chooseStyles"
    title: qsTr("Choose styles")
    initialFocus: search
    property var names: []
    property var counts: ({})
    property var checkedNames: []
    property var original: []
    // Plain objects, not a Set: V4's Set.has searches linearly.
    readonly property var checkedSet: lookup(checkedNames)
    property string actionText: ""
    readonly property var shown: {
        const f = search.text.trim().toLowerCase()
        return f.length === 0 ? names : names.filter(n => n.toLowerCase().indexOf(f) >= 0)
    }

    signal changed(var names, bool reverted)
    signal acted(var names)

    function openFor(anchorItem, list, checked, options) {
        const o = options || {}
        names = list || []
        counts = o.counts || ({})
        actionText = o.actionText || ""
        const all = lookup(names)
        checkedNames = (checked || []).filter(n => all[n] === true)
        original = (checked || []).slice()
        search.text = ""
        styleList.currentIndex = names.length > 0 ? 0 : -1
        openAt(anchorItem)
    }
    function lookup(list) {
        const set = Object.create(null)
        for (let i = 0; i < list.length; ++i)
            set[list[i]] = true
        return set
    }
    function ordered(list) {
        const wanted = lookup(list)
        return names.filter(n => wanted[n] === true)
    }
    function setChecked(list) {
        checkedNames = ordered(list)
        if (actionText.length === 0)
            changed(checkedNames, false)
    }
    function confirm() {
        if (actionText.length === 0)
            keep()
        else if (act.enabled)
            act.clicked()
    }
    function toggle(name) {
        setChecked(checkedSet[name] === true ? checkedNames.filter(n => n !== name) : checkedNames.concat([name]))
    }
    onReverted: if (actionText.length === 0) changed(original, true)

    TextField {
        id: search
        objectName: "chooseStylesSearch"
        placeholderText: qsTr("Search styles")
        Accessible.name: qsTr("Search styles")
        Layout.fillWidth: true
        Layout.preferredWidth: 236
        leftPadding: 28
        onAccepted: chooser.confirm()
        Icon {
            iconRole: "search"
            color: Theme.muted
            anchors.left: parent.left
            anchors.leftMargin: 7
            anchors.verticalCenter: parent.verticalCenter
        }
        onTextChanged: styleList.currentIndex = chooser.shown.length > 0 ? 0 : -1
        Keys.onDownPressed: {
            styleList.forceActiveFocus()
            styleList.currentIndex = Math.max(0, styleList.currentIndex)
        }
    }
    Rectangle {
        color: Theme.field
        border.color: styleList.activeFocus ? Theme.focus : Theme.line
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(styleList.contentHeight + 6, 8 * 22 + 6)
        Layout.minimumHeight: 28
        Label {
            visible: chooser.shown.length === 0
            anchors.centerIn: parent
            text: chooser.names.length === 0 ? qsTr("No styles") : qsTr("No style matches")
            color: Theme.muted
        }
        ListView {
            id: styleList
            objectName: "chooseStylesList"
            anchors.fill: parent
            anchors.margins: 3
            clip: true
            model: chooser.shown
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: chooser.title
            ScrollBar.vertical: ScrollBar { policy: styleList.contentHeight > styleList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            Keys.onSpacePressed: if (currentIndex >= 0) chooser.toggle(chooser.shown[currentIndex])
            Keys.onReturnPressed: chooser.confirm()
            Keys.onEnterPressed: chooser.confirm()
            delegate: ItemDelegate {
                id: row
                required property string modelData
                required property int index
                objectName: "chooseStylesRow_" + modelData
                width: ListView.view.width
                implicitHeight: 22
                padding: 0
                leftPadding: 6
                rightPadding: 12
                highlighted: ListView.isCurrentItem && styleList.activeFocus
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.CheckBox
                Accessible.name: modelData
                Accessible.checkable: true
                Accessible.checked: box.checked
                onClicked: {
                    styleList.currentIndex = index
                    chooser.toggle(modelData)
                }
                contentItem: RowLayout {
                    spacing: 8
                    CheckBox {
                        id: box
                        objectName: "chooseStylesCheck_" + row.modelData
                        checked: chooser.checkedSet[row.modelData] === true
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onClicked: {
                            checked = Qt.binding(() => chooser.checkedSet[row.modelData] === true)
                            styleList.currentIndex = row.index
                            chooser.toggle(row.modelData)
                        }
                    }
                    Label {
                        text: row.modelData
                        color: row.highlighted ? row.palette.highlightedText : row.palette.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: chooser.counts[row.modelData] !== undefined
                        text: Number(chooser.counts[row.modelData] || 0).toLocaleString(Qt.locale(), "f", 0)
                        color: row.highlighted ? row.palette.highlightedText : Theme.muted
                        font.pixelSize: Math.round(row.font.pixelSize * 12 / 13)
                    }
                }
            }
        }
    }
    RowLayout {
        spacing: 4
        Layout.fillWidth: true
        Button {
            objectName: "chooseStylesAll"
            flat: true
            text: qsTr("Select all")
            enabled: chooser.shown.length > 0
            onClicked: chooser.setChecked(chooser.checkedNames.concat(chooser.shown))
        }
        Button {
            objectName: "chooseStylesNone"
            flat: true
            text: qsTr("None")
            enabled: chooser.shown.length > 0
            onClicked: {
                const shown = chooser.lookup(chooser.shown)
                chooser.setChecked(chooser.checkedNames.filter(n => shown[n] !== true))
            }
        }
        Item { Layout.fillWidth: true }
        Label {
            objectName: "chooseStylesCount"
            text: qsTr("%1 of %2 selected").arg(chooser.checkedNames.length, chooser.names.length)
            color: Theme.muted
        }
    }
    PrimaryButton {
        id: act
        objectName: "chooseStylesAct"
        visible: chooser.actionText.length > 0
        enabled: chooser.checkedNames.length > 0
        text: chooser.actionText
        Layout.alignment: Qt.AlignRight
        onClicked: {
            const list = chooser.checkedNames
            chooser.keep()
            chooser.acted(list)
        }
    }
}
