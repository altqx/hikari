pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Tracking"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "extendTrackingDialog"
    required property var app
    title: qsTr("Extend tracked lines")
    dialogId: "extendTracking"
    modal: true
    defaultButton: extendButton
    initialFocus: frames
    property var initial: ({})
    property var document: 0
    property string failure: ""
    readonly property var status: visible ? app.checkExtendTracking(frames.value, at.currentIndex === 1) : ({})
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }

    function remembered() {
        try {
            const saved = JSON.parse(dialog.settings ? dialog.settings.value("tracking.extendOptions") || "{}" : "{}")
            return {
                frames: Number.isInteger(saved.frames) ? saved.frames : 4,
                after: saved.after === true,
                origin: saved.origin !== false
            }
        } catch (e) {
            return { frames: 4, after: false, origin: true }
        }
    }
    function openDialog() {
        const v = app.extendTrackingValues()
        if (v.frames === undefined)
            return
        initial = v
        const o = remembered()
        frames.value = o.frames
        at.currentIndex = o.after ? 1 : 0
        origin.checked = o.origin
        failure = ""
        document = app.editingDocument()
        open()
    }
    function extend() {
        if (!status.canApply || app.editingDocument() !== document)
            return
        failure = app.extendTrackedLines(frames.value, at.currentIndex === 1, origin.checked)
        if (failure.length > 0)
            return
        if (dialog.settings)
            dialog.settings.setValue("tracking.extendOptions",
                              JSON.stringify({ frames: frames.value, after: at.currentIndex === 1, origin: origin.checked }))
        close()
    }

    ColumnLayout {
        spacing: 10
        Layout.preferredWidth: 380
        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Label { text: qsTr("Add") }
            RowLayout {
                spacing: 6
                SpinBox {
                    id: frames
                    objectName: "extendTrackingFrames"
                    from: 1; to: 999; editable: true
                    Layout.preferredWidth: 96
                    Accessible.name: qsTr("Frames to add")
                    onValueChanged: dialog.failure = ""
                }
                Label { text: qsTr("frames") }
                ComboBox {
                    id: at
                    objectName: "extendTrackingAt"
                    model: [qsTr("before the first line"), qsTr("after the last line")]
                    Layout.preferredWidth: 170
                    Accessible.name: qsTr("Where to add them")
                    onActivated: dialog.failure = ""
                }
            }
        }
        CheckBox {
            id: origin
            objectName: "extendTrackingOrigin"
            text: qsTr("Move the origin (%1)").arg("\\org")
        }
        RowLayout {
            id: statusRow
            spacing: 6
            Layout.preferredWidth: 380
            Layout.minimumHeight: 34
            readonly property string kind: dialog.failure.length > 0 ? "error" : dialog.status.state ?? "idle"
            readonly property color tint: kind === "error" ? Theme.danger : kind === "warning" ? Theme.warning
                                          : kind === "idle" ? Theme.muted : Theme.text
            Icon {
                iconRole: ({ ok: "run-applied", warning: "notice-warning", error: "notice-error" })[statusRow.kind] || "notice-info"
                size: 14
                color: statusRow.tint
                accentColor: statusRow.kind === "ok" ? Theme.accent : statusRow.tint
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
            }
            Label {
                objectName: "extendTrackingStatus"
                text: dialog.failure.length > 0 ? dialog.failure : dialog.status.text ?? ""
                color: statusRow.tint
                wrapMode: Text.Wrap
                font: dialog.fontAt(12)
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    leadingActions: [
        Label {
            objectName: "extendTrackingLines"
            text: qsTr("%1 · frames %2–%3").arg(dialog.initial.lines ?? "", dialog.initial.firstFrame ?? 0,
                                                (dialog.initial.firstFrame ?? 0) + (dialog.initial.frames ?? 1) - 1)
            color: Theme.muted
            font: dialog.fontAt(12)
            Layout.alignment: Qt.AlignVCenter
        }
    ]
    buttons: [
        PrimaryButton {
            id: extendButton
            objectName: "extendTrackingApply"
            text: qsTr("Extend")
            enabled: dialog.status.canApply === true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.extend()
        },
        Button {
            objectName: "extendTrackingCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
