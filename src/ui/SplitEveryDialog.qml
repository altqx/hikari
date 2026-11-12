pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Grid"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "splitEveryDialog"
    required property var app
    property bool hasVideo: false
    title: qsTr("Split every")
    dialogId: "splitEvery"
    modal: true
    defaultButton: splitButton
    initialFocus: countBox
    readonly property bool seconds: unitChoice.currentIndex === 1
    readonly property bool usable: seconds || hasVideo

    function openDialog() {
        let o = {}
        try {
            o = JSON.parse(dialog.settings ? dialog.settings.value("grid.splitEvery") || "{}" : "{}")
        } catch (e) {
            o = {}
        }
        countBox.value = Number.isInteger(o.count) && o.count > 0 ? o.count : 10
        unitChoice.currentIndex = o.unit === "seconds" ? 1 : 0
        open()
    }
    function split() {
        if (!usable)
            return
        if (dialog.settings)
            dialog.settings.setValue("grid.splitEvery", JSON.stringify({ count: countBox.value,
                                                                           unit: seconds ? "seconds" : "frames" }))
        app.splitEvery(countBox.value, seconds)
        close()
    }

    ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 8
            Label { text: qsTr("Every") }
            SpinBox {
                id: countBox
                objectName: "splitEveryCount"
                from: 1
                to: 100000
                editable: true
                Accessible.name: qsTr("Piece length")
            }
            SegmentedButtons {
                id: unitChoice
                objectName: "splitEveryUnit"
                choices: [{ text: qsTr("Frames"), name: "splitEveryFrames" }, { text: qsTr("Seconds"), name: "splitEverySeconds" }]
                Accessible.name: qsTr("Unit")
                onActivated: index => currentIndex = index
            }
        }
        Label {
            objectName: "splitEveryHint"
            visible: !dialog.usable
            text: qsTr("Frames need a video.")
            color: Theme.muted
        }
    }

    buttons: [
        PrimaryButton {
            id: splitButton
            objectName: "splitEveryApply"
            text: qsTr("Split")
            enabled: dialog.usable
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.split()
        },
        Button {
            objectName: "splitEveryCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
