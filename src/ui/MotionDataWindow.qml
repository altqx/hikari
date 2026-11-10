pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Tracking"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "motionDataDialog"
    required property var app
    title: qsTr("Apply motion data")
    dialogId: "motionData"
    modal: true
    defaultButton: applyButton
    initialFocus: mainData.area
    property var initial: ({})
    property var document: 0
    property string failure: ""
    readonly property var status: visible ? app.checkMotionData(mainData.text, clipData.checked ? clipField.text : "",
                                                                 relativeFrame.value,
                                                                 extrapolate.checked ? dataStart.value : 0)
                                          : ({})
    readonly property int shortBy: Math.max(0, (initial.frames ?? 0) - (status.dataFrames ?? (initial.frames ?? 0)))
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }

    readonly property var defaults: ({
        x: true, y: true, absolute: false, scale: true, rotation: false, origin: false,
        border: true, shadow: true, blur: true, blurPercent: 100,
        rectClips: true, vectorClips: true, rectToVector: false, clipsOnly: false, clipData: false,
        bake: true, linear: false, extrapolate: false
    })
    function remembered() {
        let saved = {}
        try {
            saved = JSON.parse(dialog.settings ? dialog.settings.value("tracking.motionOptions") || "{}" : "{}")
        } catch (e) {
            saved = {}
        }
        const out = {}
        for (const key in defaults)
            out[key] = typeof saved[key] === typeof defaults[key] ? saved[key] : defaults[key]
        return out
    }
    function options() {
        return {
            x: position.checked && followX.checked, y: position.checked && followY.checked,
            absolute: position.checked && absolute.checked, scale: scale.checked, rotation: rotation.checked,
            origin: origin.checked, border: border.checked, shadow: shadow.checked, blur: blur.checked,
            blurPercent: blurPercent.value, rectClips: rectClips.checked, vectorClips: vectorClips.checked,
            rectToVector: rectToVector.checked, clipsOnly: clipsOnly.checked, clipData: clipData.checked,
            bake: bake.checked, linear: linear.checked, extrapolate: extrapolate.checked,
            relativeFrame: relativeFrame.value, dataStart: extrapolate.checked ? dataStart.value : 0
        }
    }

    function openDialog() {
        const v = app.motionDataValues()
        if (v.frames === undefined)
            return
        initial = v
        const o = remembered()
        mainData.text = v.data
        clipField.text = ""
        relativeFrame.value = v.relativeFrame
        position.checked = o.x || o.y
        followX.checked = o.x || !o.y
        followY.checked = o.y || !o.x
        absolute.checked = o.absolute
        scale.checked = o.scale
        rotation.checked = o.rotation
        origin.checked = o.origin
        border.checked = o.border
        shadow.checked = o.shadow
        blur.checked = o.blur
        blurPercent.value = o.blurPercent
        rectClips.checked = o.rectClips
        vectorClips.checked = o.vectorClips
        rectToVector.checked = o.rectToVector
        clipsOnly.checked = o.clipsOnly
        clipData.checked = o.clipData
        bake.checked = o.bake
        linear.checked = o.linear
        extrapolate.checked = o.extrapolate
        dataStart.value = 1
        failure = ""
        document = app.editingDocument()
        open()
    }
    function apply() {
        if (!status.canApply || app.editingDocument() !== document)
            return
        const o = options()
        failure = app.applyMotionData(mainData.text, clipField.text, o)
        if (failure.length > 0)
            return
        if (dialog.settings) {
            const kept = {}
            for (const key in defaults)
                kept[key] = o[key]
            dialog.settings.setValue("tracking.motionOptions", JSON.stringify(kept))
        }
        close()
    }

    component Rule: Rectangle {
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
    }
    component Heading: Label {
        color: Theme.muted
        font: dialog.fontAt(12, Font.DemiBold)
        Layout.topMargin: 2
    }

    RowLayout {
        spacing: 24
        ColumnLayout {
            spacing: 10
            Layout.alignment: Qt.AlignTop
            TrackingDataField {
                id: mainData
                app: dialog.app
                host: dialog
                title: qsTr("Motion data")
                boxHeight: clipData.checked ? 150 : 236
                dataObjectName: "motionDataText"
                statusObjectName: "motionDataStatus"
                pasteObjectName: "motionDataPaste"
                openObjectName: "motionDataOpen"
                statusKind: dialog.failure.length > 0 ? "error" : dialog.status.state ?? "idle"
                statusText: dialog.failure.length > 0 ? dialog.failure : dialog.status.text ?? ""
                onTextChanged: dialog.failure = ""
            }
            TrackingDataField {
                id: clipField
                visible: clipData.checked
                app: dialog.app
                host: dialog
                title: qsTr("Clip data")
                boxHeight: 110
                dataObjectName: "motionDataClipText"
                statusObjectName: "motionDataClipStatus"
                pasteObjectName: "motionDataClipPaste"
                openObjectName: "motionDataClipOpen"
                statusKind: dialog.status.clipState ?? "idle"
                statusText: dialog.status.clipText ?? ""
                onTextChanged: dialog.failure = ""
            }
        }
        ColumnLayout {
            spacing: 8
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 22
            GridLayout {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Label { text: qsTr("Relative to frame") }
                RowLayout {
                    spacing: 6
                    SpinBox {
                        id: relativeFrame
                        objectName: "motionDataFrame"
                        from: 1; to: Math.max(1, dialog.initial.frames ?? 1); editable: true
                        Layout.preferredWidth: 96
                        Accessible.name: qsTr("Relative to frame")
                    }
                    Label {
                        text: qsTr("of %1 · video frame %2").arg(dialog.initial.frames ?? 0,
                                                                 (dialog.initial.firstFrame ?? 0) + relativeFrame.value - 1)
                        color: Theme.muted
                        font: dialog.fontAt(12)
                    }
                }
            }
            Rule {}
            Heading { text: qsTr("Follow") }
            RowLayout {
                spacing: 4
                CheckBox { id: position; objectName: "motionDataPosition"; text: qsTr("Position") }
                CheckBox { id: absolute; objectName: "motionDataAbsolute"; text: qsTr("Absolute"); enabled: position.checked }
            }
            RowLayout {
                spacing: 4
                Layout.leftMargin: 24
                enabled: position.checked
                CheckBox {
                    id: followX
                    objectName: "motionDataX"
                    text: "x"
                    Accessible.name: qsTr("Position x")
                    onToggled: if (!checked && !followY.checked) followY.checked = true
                }
                CheckBox {
                    id: followY
                    objectName: "motionDataY"
                    text: "y"
                    Accessible.name: qsTr("Position y")
                    onToggled: if (!checked && !followX.checked) followX.checked = true
                }
            }
            RowLayout {
                spacing: 4
                CheckBox { id: scale; objectName: "motionDataScale"; text: qsTr("Scale") }
                CheckBox { id: rotation; objectName: "motionDataRotation"; text: qsTr("Rotation") }
                CheckBox { id: origin; objectName: "motionDataOrigin"; text: qsTr("Origin") }
            }
            RowLayout {
                spacing: 4
                Layout.leftMargin: 24
                enabled: scale.checked
                CheckBox { id: border; objectName: "motionDataBorder"; text: qsTr("Outline") }
                CheckBox { id: shadow; objectName: "motionDataShadow"; text: qsTr("Shadow") }
                CheckBox { id: blur; objectName: "motionDataBlur"; text: qsTr("Blur") }
                SpinBox {
                    id: blurPercent
                    objectName: "motionDataBlurPercent"
                    from: 0; to: 200; editable: true
                    enabled: blur.checked
                    Layout.preferredWidth: 84
                    Accessible.name: qsTr("Blur factor, percent")
                }
                Label { text: "%"; color: Theme.muted }
            }
            Rule {}
            Heading { text: qsTr("Clips") }
            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 2
                CheckBox { id: rectClips; objectName: "motionDataRectClips"; text: qsTr("Rect clips") }
                CheckBox { id: vectorClips; objectName: "motionDataVectorClips"; text: qsTr("Vector clips") }
                CheckBox { id: rectToVector; objectName: "motionDataRectToVector"; text: qsTr("Rect to vector") }
                CheckBox { id: clipsOnly; objectName: "motionDataClipsOnly"; text: qsTr("Clips only") }
            }
            CheckBox { id: clipData; objectName: "motionDataClipData"; text: qsTr("Track clips with other data") }
            Rule {}
            CheckBox { id: bake; objectName: "motionDataBake"; text: qsTr("Bake transforms and fades") }
            CheckBox { id: linear; objectName: "motionDataLinear"; text: qsTr("One line with %1 and %2").arg("\\move", "\\t") }
            CheckBox {
                id: extrapolate
                objectName: "motionDataExtrapolate"
                text: qsTr("Extrapolate past the data")
                enabled: checked || dialog.shortBy > 0
            }
            RowLayout {
                visible: extrapolate.checked
                spacing: 6
                Layout.leftMargin: 24
                Label { text: qsTr("Data starts at frame") }
                SpinBox {
                    id: dataStart
                    objectName: "motionDataStart"
                    from: 1; to: dialog.shortBy + 1; editable: true
                    Layout.preferredWidth: 96
                    Accessible.name: qsTr("Data starts at frame")
                }
            }
        }
    }

    leadingActions: [
        Label {
            objectName: "motionDataLines"
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
            objectName: "motionDataApply"
            text: qsTr("Apply")
            enabled: dialog.status.canApply === true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "motionDataCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
