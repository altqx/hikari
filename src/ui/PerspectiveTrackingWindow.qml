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
    initialFocus: dataField.area
    property var initial: ({})
    property var document: 0
    property string failure: ""
    readonly property var status: visible ? app.checkPerspectiveTracking(dataField.text, relativeFrame.value) : ({})
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
        dataField.text = v.data
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
        failure = app.applyPerspectiveTracking(dataField.text, relativeFrame.value, origin.currentIndex, fitFlat.checked,
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
        TrackingDataField {
            id: dataField
            app: dialog.app
            host: dialog
            title: qsTr("Power-Pin data")
            dataObjectName: "perspectiveTrackingData"
            statusObjectName: "perspectiveTrackingStatus"
            pasteObjectName: "perspectiveTrackingPaste"
            openObjectName: "perspectiveTrackingOpen"
            statusKind: dialog.failure.length > 0 ? "error" : dialog.status.state ?? "idle"
            statusText: dialog.failure.length > 0 ? dialog.failure : dialog.status.text ?? ""
            Layout.alignment: Qt.AlignTop
            onTextChanged: dialog.failure = ""
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
