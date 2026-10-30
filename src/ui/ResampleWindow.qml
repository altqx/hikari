pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Conversion"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "resampleDialog"
    required property var app
    title: qsTr("Resample subtitles")
    dialogId: "resample"
    modal: true
    defaultButton: resampleButton
    initialFocus: targetWidth
    property var initial: ({})
    property bool stretch: false
    property var document: 0
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }
    property var video: null
    Connections {
        target: dialog.video
        function onChanged() { if (dialog.visible) dialog.refreshVideo() }
    }
    function refreshVideo() {
        const v = app.resampleValues()
        if (v.subsWidth === undefined || (v.hasVideo === initial.hasVideo && v.videoWidth === initial.videoWidth
                                          && v.videoHeight === initial.videoHeight))
            return
        initial = Object.assign({}, initial, { hasVideo: v.hasVideo, videoWidth: v.videoWidth, videoHeight: v.videoHeight })
    }

    function openDialog() {
        const v = app.resampleValues()
        if (v.subsWidth === undefined)
            return
        initial = v
        subsWidth.value = v.subsWidth
        subsHeight.value = v.subsHeight
        targetWidth.value = v.videoWidth
        targetHeight.value = v.videoHeight
        stretch = false
        document = app.editingDocument()
        open()
    }
    readonly property bool aspectChanges: targetWidth.value / subsWidth.value !== targetHeight.value / subsHeight.value
    readonly property bool same: subsWidth.value === targetWidth.value && subsHeight.value === targetHeight.value
    function scale(v) { return String(Math.round(v * 1000) / 1000) }
    function ratio(w, h) {
        let a = w, b = h
        while (b) [a, b] = [b, a % b]
        return w / a <= 32 && h / a <= 32 ? (w / a) + ":" + (h / a) : scale(w / h) + ":1"
    }
    function resample() {
        if (same || app.editingDocument() !== document)
            return
        const v = app.resampleValues()
        if (v.subsWidth !== initial.subsWidth || v.subsHeight !== initial.subsHeight) {
            const untouched = subsWidth.value === initial.subsWidth && subsHeight.value === initial.subsHeight
            initial = v
            if (untouched) {
                subsWidth.value = v.subsWidth
                subsHeight.value = v.subsHeight
                return
            }
        }
        app.resample(subsWidth.value, subsHeight.value, targetWidth.value, targetHeight.value, aspectChanges && stretch)
        close()
    }

    component Hint: Label {
        Layout.fillWidth: true
        Layout.preferredWidth: 200
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
    }
    // A disabled Button gets no hover, so the wrapper shows the tooltip.
    component FromButton: Item {
        id: from
        required property SpinBox widthBox
        required property SpinBox heightBox
        property alias text: button.text
        property int w
        property int h
        property string why: ""
        implicitWidth: button.implicitWidth
        implicitHeight: button.implicitHeight
        Button {
            id: button
            objectName: from.objectName + "Button"
            anchors.fill: parent
            enabled: from.why.length === 0 && (from.widthBox.value !== from.w || from.heightBox.value !== from.h)
            Accessible.description: from.why
            onClicked: {
                dialog.refreshVideo()
                from.widthBox.value = from.w
                from.heightBox.value = from.h
            }
        }
        HoverHandler { id: hover }
        ToolTip.visible: why.length > 0 && hover.hovered
        ToolTip.text: why
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 6
        Layout.fillWidth: true
        Label { text: qsTr("Subtitles") }
        RowLayout {
            spacing: 6
            SpinBox {
                id: subsWidth
                objectName: "resampleSubsWidth"
                from: 100; to: 13000; editable: true
                Accessible.name: qsTr("Subtitles width")
            }
            Label { text: "×" }
            SpinBox {
                id: subsHeight
                objectName: "resampleSubsHeight"
                from: 100; to: 10000; editable: true
                Accessible.name: qsTr("Subtitles height")
            }
            FromButton {
                objectName: "resampleSubsFromSubtitles"
                text: qsTr("From subtitles")
                widthBox: subsWidth; heightBox: subsHeight
                w: dialog.initial.subsWidth ?? 0; h: dialog.initial.subsHeight ?? 0
            }
            FromButton {
                objectName: "resampleSubsFromVideo"
                text: qsTr("From video")
                widthBox: subsWidth; heightBox: subsHeight
                w: dialog.initial.videoWidth ?? 0; h: dialog.initial.videoHeight ?? 0
                why: dialog.initial.hasVideo === true ? "" : qsTr("No video is open")
            }
        }
        Item { implicitWidth: 1 }
        Hint { text: qsTr("The resolution the lines are written for now.") }
        Label { text: qsTr("Target") }
        RowLayout {
            spacing: 6
            SpinBox {
                id: targetWidth
                objectName: "resampleWidth"
                from: 100; to: 13000; editable: true
                Accessible.name: qsTr("Target width")
            }
            Label { text: "×" }
            SpinBox {
                id: targetHeight
                objectName: "resampleHeight"
                from: 100; to: 10000; editable: true
                Accessible.name: qsTr("Target height")
            }
            FromButton {
                objectName: "resampleTargetFromSubtitles"
                text: qsTr("From subtitles")
                widthBox: targetWidth; heightBox: targetHeight
                w: dialog.initial.subsWidth ?? 0; h: dialog.initial.subsHeight ?? 0
            }
            FromButton {
                objectName: "resampleTargetFromVideo"
                text: qsTr("From video")
                widthBox: targetWidth; heightBox: targetHeight
                w: dialog.initial.videoWidth ?? 0; h: dialog.initial.videoHeight ?? 0
                why: dialog.initial.hasVideo === true ? "" : qsTr("No video is open")
            }
        }
        Item { implicitWidth: 1 }
        Hint {
            objectName: "resampleScale"
            text: dialog.same ? qsTr("The same as the subtitles: nothing to resample.")
                : !dialog.aspectChanges ? qsTr("Positions, sizes and margins are scaled by %1.")
                                            .arg(dialog.scale(targetWidth.value / subsWidth.value))
                : qsTr("Scaled by %1 across and %2 down.").arg(dialog.scale(targetWidth.value / subsWidth.value))
                                                          .arg(dialog.scale(targetHeight.value / subsHeight.value))
        }
    }
    FormSection {
        objectName: "resampleAspectSection"
        title: qsTr("Aspect ratio")
        visible: dialog.aspectChanges
        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            SegmentedButtons {
                objectName: "resampleAspect"
                Layout.preferredWidth: 240
                choices: [{ text: qsTr("Keep it"), name: "resampleKeep" },
                          { text: qsTr("Stretch"), name: "resampleStretch" }]
                currentIndex: dialog.stretch ? 1 : 0
                onActivated: i => dialog.stretch = i === 1
            }
            Hint {
                text: qsTr("The aspect ratio changes from %1 to %2.").arg(dialog.ratio(subsWidth.value, subsHeight.value))
                                                                      .arg(dialog.ratio(targetWidth.value, targetHeight.value))
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: resampleButton
            objectName: "resampleOk"
            text: qsTr("Resample")
            enabled: !dialog.same
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.resample()
        },
        Button {
            objectName: "resampleCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
