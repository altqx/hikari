pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: box
    objectName: "messageBox"
    modal: true
    property string severity: "info"
    property string text: ""
    property string detail: ""
    property string details: ""
    property bool plainText: false
    property bool offerDontAsk: false
    property var choices: []
    property var done: null
    property bool showDetails: false
    readonly property bool dontAskAgain: dontAskBox.checked

    signal answered(string id, bool dontAskAgain)

    function ask(options, callback) {
        const o = options || {}
        title = o.title || ""
        text = o.text || ""
        detail = o.detail || ""
        details = o.details || ""
        plainText = o.plainText === true
        severity = o.severity || "info"
        offerDontAsk = o.dontAsk === true
        dontAskBox.checked = false
        showDetails = false
        choices = o.choices || [{ id: "ok", text: qsTr("OK"), role: "accept", isDefault: true }]
        done = callback || null
        openFrom(o.from || null)
    }
    function answer(id) {
        const callback = done
        done = null
        const again = offerDontAsk && dontAskBox.checked
        if (id === rejectId())
            reject()
        else
            accept()
        answered(id, again)
        if (callback)
            callback(id, again)
    }
    function rejectId() {
        const r = choices.find(c => c.role === "reject")
        return r ? r.id : ""
    }
    function colourOf(s) { return s === "error" ? Theme.danger : s === "warning" ? Theme.warning : IconTheme.normal }
    function roleOf(c) {
        return c.role === "reject" ? DialogButtonBox.RejectRole
             : c.role === "destructive" ? DialogButtonBox.DestructiveRole
             : c.role === "action" ? DialogButtonBox.ActionRole
             : DialogButtonBox.AcceptRole
    }
    function defaultIndex() {
        const i = choices.findIndex(c => c.isDefault === true && c.role !== "destructive")
        return i >= 0 ? i : choices.findIndex(c => c.role === "reject")
    }
    onRejected: {
        if (done !== null) {
            const callback = done
            done = null
            const id = rejectId()
            answered(id, false)
            callback(id, false)
        }
    }

    RowLayout {
        spacing: 16
        Layout.preferredWidth: 420
        Layout.topMargin: 8
        Icon {
            objectName: "messageBoxIcon"
            iconRole: ({ error: "notice-error", warning: "notice-warning", info: "notice-info" })[box.severity] || "notice-info"
            size: 32
            color: box.colourOf(box.severity)
            accentColor: box.severity === "info" ? IconTheme.accent : color
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            Layout.topMargin: 4
            Label {
                objectName: "messageBoxText"
                text: box.text
                font: box.fontAt(14, Font.DemiBold)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
            }
            Label {
                objectName: "messageBoxDetail"
                visible: box.detail.length > 0
                text: box.detail
                color: Theme.muted
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            CheckBox {
                id: dontAskBox
                objectName: "messageBoxDontAsk"
                visible: box.offerDontAsk
                text: qsTr("Don't ask again")
                Layout.topMargin: 8
            }
        }
    }
    ColumnLayout {
        visible: box.details.length > 0
        spacing: 6
        Layout.leftMargin: 48
        Layout.fillWidth: true
        Button {
            objectName: "messageBoxDisclosure"
            flat: true
            text: qsTr("Details")
            Accessible.role: Accessible.ButtonDropDown
            Accessible.description: box.showDetails ? qsTr("Expanded") : qsTr("Collapsed")
            onClicked: box.showDetails = !box.showDetails
            contentItem: RowLayout {
                spacing: 4
                Icon { iconRole: box.showDetails ? "chevron-down" : "chevron-right"; size: 14 }
                Label { text: qsTr("Details") }
            }
        }
        Rectangle {
            visible: box.showDetails
            color: Theme.field
            border.color: Theme.line
            implicitHeight: Math.min(detailsText.implicitHeight + 16, 220)
            Layout.fillWidth: true
            ScrollView {
                id: detailsScroll
                objectName: "messageBoxDetailsScroll"
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                anchors.rightMargin: 38
                contentWidth: availableWidth
                Label {
                    id: detailsText
                    objectName: "messageBoxDetails"
                    width: detailsScroll.availableWidth
                    text: box.details
                    font.family: "monospace"
                    wrapMode: Text.WrapAnywhere
                    textFormat: Text.PlainText
                }
            }
            IconToolButton {
                objectName: "messageBoxCopyDetails"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                iconRole: "edit-copy"
                text: qsTr("Copy details")
                onClicked: box.copyDetails()
            }
        }
    }

    property var made: []
    onChoicesChanged: makeButtons()
    function makeButtons() {
        for (const b of made) {
            buttonBox.removeItem(b)
            b.destroy()
        }
        const list = []
        const d = defaultIndex()
        choices.forEach((c, i) => {
            const kind = i === d ? primary : c.role === "destructive" ? danger : plain
            const b = kind.createObject(null, { text: c.text, choiceId: c.id, choiceRole: roleOf(c),
                                               objectName: "messageBoxChoice_" + c.id })
            buttonBox.addItem(b)
            list.push(b)
        })
        made = list
        defaultButton = d >= 0 ? list[d] : null
    }
    Component {
        id: primary
        PrimaryButton {
            property string choiceId
            property int choiceRole
            DialogButtonBox.buttonRole: choiceRole
            onClicked: box.answer(choiceId)
        }
    }
    Component {
        id: danger
        DangerButton {
            property string choiceId
            property int choiceRole
            DialogButtonBox.buttonRole: choiceRole
            onClicked: box.answer(choiceId)
        }
    }
    Component {
        id: plain
        Button {
            property string choiceId
            property int choiceRole
            DialogButtonBox.buttonRole: choiceRole
            onClicked: box.answer(choiceId)
        }
    }
    // QML has no clipboard API; a hidden TextEdit copies.
    TextEdit { id: copier; visible: false }
    function copyDetails() {
        copier.text = details
        copier.selectAll()
        copier.copy()
    }
}
