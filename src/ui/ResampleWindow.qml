// Y4, PU6 (#308): legacy SubsResampleDialog ("Change resolution") as
// Subtitles > Resample subtitles, on DG1's frame, fixed, modal to the main
// window. Both rows get From subtitles and From video (legacy one button a
// row), each off while its values are already there, From video also
// without a video (its tooltip says so); the hint under the target says the
// scale. "Aspect ratio" (legacy "Resample options", No stretch / Stretch)
// shows only when the aspect ratio changes; Resample is off while both
// resolutions match (legacy OK did nothing).
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
    title: qsTr("Resample subtitles") // the menu command's name (legacy: "Change resolution")
    dialogId: "resample"
    modal: true
    defaultButton: resampleButton
    initialFocus: targetWidth
    property var initial: ({})
    // Stretch chosen (legacy's "Stretch" radio).
    property bool stretch: false
    // The Document it opened for; another editing target closes it (as
    // ScriptPropertiesDialog).
    property var document: 0
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
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
    // The stretch choice only means something when the aspect ratio changes.
    readonly property bool aspectChanges: targetWidth.value / subsWidth.value !== targetHeight.value / subsHeight.value
    readonly property bool same: subsWidth.value === targetWidth.value && subsHeight.value === targetHeight.value
    function scale(v) { return String(Math.round(v * 1000) / 1000) }
    // 16:9 for 1920 × 1080; 1.78:1 when the reduced terms are large.
    function ratio(w, h) {
        let a = w, b = h
        while (b) [a, b] = [b, a % b]
        return w / a <= 32 && h / a <= 32 ? (w / a) + ":" + (h / a) : scale(w / h) + ":1"
    }
    function resample() {
        if (same || app.editingDocument() !== document)
            return
        // The subtitles' resolution can change while this is open (the main
        // window stays usable beside a floating panel): source sizes left as
        // they opened follow it and are shown before anything is resampled.
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
    // From subtitles / From video: sets the row to `w` × `h`. `why` turns
    // it off and says why on hover, over the wrapper (a disabled button gets
    // no hover).
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
                from.widthBox.value = from.w
                from.heightBox.value = from.h
            }
        }
        HoverHandler { id: hover }
        ToolTip.visible: why.length > 0 && hover.hovered
        ToolTip.text: why
        ToolTip.delay: 500
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
