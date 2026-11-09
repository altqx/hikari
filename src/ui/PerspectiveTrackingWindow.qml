pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Tracking"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "perspectiveTrackingDialog"
    required property var app
    title: qsTr("Perspective tracking")
    dialogId: "perspectiveTracking"
    modal: true
    defaultButton: applyButton
    initialFocus: dataText
    property var initial: ({})
    property var document: 0
    property string failure: ""
    readonly property var status: visible ? app.checkPerspectiveTracking(dataText.text, relativeFrame.value) : ({})
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }

    function openDialog() {
        const v = app.perspectiveTrackingValues()
        if (v.frames === undefined)
            return
        initial = v
        dataText.text = v.data
        relativeFrame.value = v.relativeFrame
        origin.currentIndex = 2
        fitFlat.checked = v.fitFlat
        moveClips.checked = true
        scaleBorders.checked = true
        failure = ""
        document = app.editingDocument()
        open()
    }
    function apply() {
        if (!status.canApply || app.editingDocument() !== document)
            return
        failure = app.applyPerspectiveTracking(dataText.text, relativeFrame.value, origin.currentIndex, fitFlat.checked,
                                               moveClips.checked, scaleBorders.checked)
        if (failure.length === 0)
            close()
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
    }

    RowLayout {
        spacing: 24
        ColumnLayout {
            spacing: 6
            Layout.alignment: Qt.AlignTop
            Label {
                text: qsTr("Power-Pin data")
                font: dialog.fontAt(12, Font.DemiBold)
            }
            Item {
                implicitWidth: 400
                implicitHeight: 236
                ScrollView {
                    anchors.fill: parent
                    TextArea {
                        id: dataText
                        objectName: "perspectiveTrackingData"
                        textFormat: TextEdit.PlainText
                        selectByMouse: true
                        font.family: "monospace"
                        Accessible.name: qsTr("Power-Pin data")
                        Accessible.description: dialog.status.text ?? ""
                        background: Rectangle {
                            color: Theme.field
                            border.width: 1
                            border.color: parent.activeFocus ? Theme.focus : Theme.line
                        }
                        onTextChanged: dialog.failure = ""
                    }
                }
            }
            RowLayout {
                spacing: 6
                Layout.preferredWidth: 400
                Layout.minimumHeight: 34
                readonly property string kind: dialog.failure.length > 0 ? "error" : dialog.status.state ?? "idle"
                readonly property color tint: kind === "error" ? Theme.danger : kind === "warning" ? Theme.warning
                                              : kind === "idle" ? Theme.muted : Theme.text
                Icon {
                    iconRole: ({ ok: "run-applied", warning: "notice-warning", error: "notice-error" })[parent.kind] || "notice-info"
                    size: 14
                    color: parent.tint
                    accentColor: parent.kind === "ok" ? Theme.accent : parent.tint
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                }
                Label {
                    objectName: "perspectiveTrackingStatus"
                    text: dialog.failure.length > 0 ? dialog.failure : dialog.status.text ?? ""
                    color: parent.tint
                    wrapMode: Text.Wrap
                    font: dialog.fontAt(12)
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
        ColumnLayout {
            spacing: 10
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 22
            Layout.preferredWidth: 300
            GridLayout {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Label { text: qsTr("Relative to frame") }
                RowLayout {
                    spacing: 6
                    SpinBox {
                        id: relativeFrame
                        objectName: "perspectiveTrackingFrame"
                        from: 1; to: Math.max(1, dialog.initial.frames ?? 1); editable: true
                        Accessible.name: qsTr("Relative to frame")
                    }
                    Label {
                        text: qsTr("of %1 · video frame %2").arg(dialog.initial.frames ?? 0,
                                                                 (dialog.initial.firstFrame ?? 0) + relativeFrame.value - 1)
                        color: Theme.muted
                        font: dialog.fontAt(12)
                    }
                }
                Label { text: qsTr("Origin") }
                ComboBox {
                    id: origin
                    objectName: "perspectiveTrackingOrigin"
                    model: [qsTr("Centre"), qsTr("No %1").arg("\\fax"), qsTr("Keep")]
                    currentIndex: 2
                    Layout.preferredWidth: 120
                    Accessible.name: qsTr("Origin")
                }
            }
            Rectangle {
                color: Theme.line
                implicitHeight: 1
                Layout.fillWidth: true
            }
            CheckBox {
                id: fitFlat
                objectName: "perspectiveTrackingFitFlat"
                text: qsTr("Fit flat lines to the plane first")
            }
            CheckBox {
                id: moveClips
                objectName: "perspectiveTrackingClips"
                text: qsTr("Move clips with the plane")
                checked: true
            }
            CheckBox {
                id: scaleBorders
                objectName: "perspectiveTrackingBorders"
                text: qsTr("Scale outline and shadow")
                checked: true
            }
            Hint { text: qsTr("The plane is saved with each line, for the Perspective tool and Derive Perspective Track.") }
        }
    }

    leadingActions: [
        Label {
            objectName: "perspectiveTrackingLines"
            text: qsTr("%1 · frames %2–%3").arg(dialog.initial.lines ?? "", dialog.initial.firstFrame ?? 0,
                                                (dialog.initial.firstFrame ?? 0) + (dialog.initial.frames ?? 1) - 1)
            color: Theme.muted
            font: dialog.fontAt(12)
            Layout.alignment: Qt.AlignVCenter
        }
    ]
    buttons: [
        PrimaryButton {
            id: applyButton
            objectName: "perspectiveTrackingApply"
            text: qsTr("Apply")
            enabled: dialog.status.canApply === true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "perspectiveTrackingCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
