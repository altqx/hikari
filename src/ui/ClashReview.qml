pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: review
    objectName: "clashReview"
    modal: true
    resizable: true
    property string text: ""
    property string detail: ""
    property var rows: []
    property bool canRename: true
    property bool canMerge: false
    property string nameHeading: qsTr("Name")
    property string differenceHeading: qsTr("Difference")
    property var actions: []
    property var names: []
    property int added: 0
    property var acceptText: null
    property var nameTaken: null
    property var namePattern: /[^,]*/
    property var done: null
    readonly property int written: added + actions.filter(a => a !== "skip").length
    readonly property var kinds: (canMerge ? ["merge"] : []).concat(canRename ? ["replace", "rename", "skip"] : ["replace", "skip"])
    function choicesFor(prefix) {
        const text = { merge: qsTr("Merge"), replace: qsTr("Replace"), rename: qsTr("Rename"), skip: qsTr("Skip") }
        return kinds.map(k => ({ text: text[k], name: prefix + "_" + k }))
    }

    function review(options, callback) {
        const o = options || {}
        title = o.title || ""
        text = o.text || ""
        detail = o.detail || ""
        canRename = o.canRename !== false
        canMerge = o.canMerge === true
        nameHeading = o.nameHeading || qsTr("Name")
        differenceHeading = o.differenceHeading || qsTr("Difference")
        added = o.added || 0
        acceptText = o.acceptText || null
        nameTaken = o.nameTaken || null
        namePattern = o.namePattern || /[^,]*/
        const first = o.action || "replace"
        rows = o.rows || []
        actions = rows.map(() => first)
        names = rows.map(r => r.suggestion || "")
        done = callback || null
        open()
    }
    function setAction(i, a) {
        const list = actions.slice()
        list[i] = a
        actions = list
    }
    function setAll(a) { actions = rows.map(() => a) }
    function setName(i, n) {
        const list = names.slice()
        list[i] = n
        names = list
    }
    function finish(ok) {
        const callback = done
        done = null
        if (ok)
            accept()
        else
            reject()
        if (callback)
            callback(ok ? rows.map((r, i) => ({ name: r.name, occurrence: r.occurrence || 0, action: actions[i], newName: names[i] })) : null)
    }
    onRejected: {
        if (done !== null) {
            const callback = done
            done = null
            callback(null)
        }
    }
    function taken(i) {
        const name = (names[i] || "").trim()
        return actions[i] === "rename" && name.length > 0 && nameTaken !== null && nameTaken(name, i) === true
    }
    readonly property bool complete: actions.every((a, i) => a !== "rename" || (names[i].trim().length > 0 && !taken(i)))

    Label {
        textFormat: Text.PlainText
        objectName: "clashReviewText"
        text: review.text
        font: review.fontAt(14, Font.DemiBold)
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredWidth: 640
        Layout.topMargin: 6
    }
    Label {
        textFormat: Text.PlainText
        objectName: "clashReviewDetail"
        visible: review.detail.length > 0
        text: review.detail
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.topMargin: -6
    }
    RowLayout {
        spacing: 10
        Label { text: qsTr("All:") }
        SegmentedButtons {
            objectName: "clashReviewAll"
            choices: review.choicesFor("clashReviewAll")
            currentIndex: review.actions.length > 0 && review.actions.every(a => a === review.actions[0])
                          ? review.kinds.indexOf(review.actions[0]) : -1
            Accessible.name: qsTr("Action for every row")
            onActivated: index => review.setAll(review.kinds[index])
        }
    }
    Rectangle {
        color: Theme.field
        border.color: Theme.line
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Math.min(list.contentHeight + columnHeader.height + 2, 120)
        Layout.preferredHeight: Math.min(list.contentHeight + columnHeader.height + 2, 320)
        Rectangle {
            id: columnHeader
            x: 1; y: 1
            width: parent.width - 2
            height: 28
            color: Theme.raised
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                spacing: 10
                Label { text: review.nameHeading; color: Theme.muted; Layout.preferredWidth: 120 }
                Label { text: review.differenceHeading; color: Theme.muted; Layout.preferredWidth: 150 }
                Label { text: qsTr("Action"); color: Theme.muted; Layout.fillWidth: true }
            }
        }
        ListView {
            id: list
            objectName: "clashReviewRows"
            x: 1
            y: columnHeader.height + 1
            width: parent.width - 2
            height: parent.height - columnHeader.height - 2
            clip: true
            model: review.rows
            activeFocusOnTab: false
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: clash
                required property var modelData
                required property int index
                objectName: "clashRow" + index
                width: ListView.view.width
                implicitHeight: 44
                Rectangle {
                    visible: clash.index > 0
                    width: parent.width
                    height: 1
                    color: Theme.line
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Label {
                        textFormat: Text.PlainText
                        text: clash.modelData.name
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.preferredWidth: 120
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: clash.modelData.difference || ""
                        color: Theme.muted
                        elide: Text.ElideRight
                        Layout.preferredWidth: 150
                    }
                    SegmentedButtons {
                        objectName: "clashAction" + clash.index
                        choices: review.choicesFor("clashAction" + clash.index)
                        currentIndex: review.kinds.indexOf(review.actions[clash.index])
                        Accessible.name: qsTr("Action for %1").arg(clash.modelData.name)
                        onActivated: index => review.setAction(clash.index, review.kinds[index])
                    }
                    TextField {
                        objectName: "clashName" + clash.index
                        visible: review.actions[clash.index] === "rename"
                        text: review.names[clash.index] || ""
                        Accessible.name: qsTr("New name for %1").arg(clash.modelData.name)
                        validator: RegularExpressionValidator { regularExpression: review.namePattern }
                        Layout.fillWidth: true
                        Layout.minimumWidth: 120
                        onTextEdited: review.setName(clash.index, text)
                        // The field takes Enter itself, so it does what the window's default button would.
                        onAccepted: if (review.complete) review.finish(true)
                    }
                    Label {
                        objectName: "clashNameTaken" + clash.index
                        textFormat: Text.PlainText
                        visible: review.taken(clash.index)
                        text: qsTr("Already exists")
                        color: Theme.warning
                    }
                    Item { visible: review.actions[clash.index] !== "rename"; Layout.fillWidth: true }
                }
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: apply
            objectName: "clashReviewApply"
            enabled: review.complete
            text: review.acceptText ? review.acceptText(review.written) : qsTr("Apply")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: review.finish(true)
        },
        Button {
            objectName: "clashReviewCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: review.finish(false)
        }
    ]
    defaultButton: apply
}
