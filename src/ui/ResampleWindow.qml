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
    property string aspect: "keep"
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
        aspect = "keep"
        sameOnBothSides.checked = true
        edgesSection.expanded = false
        setEdges([0, 0, 0, 0])
        matrix.currentIndex = 0
        adjustRotations.checked = true
        document = app.editingDocument()
        open()
    }
    readonly property bool aspectChanges: targetWidth.value / subsWidth.value !== targetHeight.value / subsHeight.value
    readonly property bool shapeDiffers:
        Math.abs((targetWidth.value / subsWidth.value) / (targetHeight.value / subsHeight.value) - 1) >= 0.01
    readonly property var edges: shapeDiffers ? [edgeLeft.value, edgeRight.value, edgeTop.value, edgeBottom.value]
                                              : [0, 0, 0, 0]
    property bool fillingEdges: false
    function setEdges(e) {
        fillingEdges = true
        edgeLeft.value = e[0]
        edgeRight.value = e[1]
        edgeTop.value = e[2]
        edgeBottom.value = e[3]
        fillingEdges = false
    }
    function fitEdges() {
        if (aspect === "letterbox" || aspect === "crop")
            setEdges(app.resampleEdges(subsWidth.value, subsHeight.value, targetWidth.value, targetHeight.value,
                                       aspect === "crop"))
        else if (aspect !== "custom")
            setEdges([0, 0, 0, 0])
    }
    onAspectChanged: fitEdges()
    function edgeEdited() {
        if (fillingEdges)
            return
        if (sameOnBothSides.checked) {
            fillingEdges = true
            edgeRight.value = edgeLeft.value
            edgeBottom.value = edgeTop.value
            fillingEdges = false
        }
        aspect = "custom"
    }
    readonly property var aspectHints: ({
        keep: qsTr("Text keeps its proportions. Positions follow the new picture."),
        stretch: qsTr("Text stretches with the picture."),
        letterbox: qsTr("The video adds bars around the picture the subtitles were made for. Signs stay on what they mark."),
        crop: qsTr("The video cuts off the edges of the picture the subtitles were made for. Signs stay on what they mark."),
        custom: qsTr("Custom edges. Text keeps its proportions.")
    })
    readonly property string edgesSummary: {
        const parts = []
        const add = (value, text) => { if (value !== 0) parts.push(text.arg(value)) }
        add(edgeLeft.value, qsTr("Left %1"))
        add(edgeRight.value, qsTr("Right %1"))
        add(edgeTop.value, qsTr("Top %1"))
        add(edgeBottom.value, qsTr("Bottom %1"))
        return parts.join(" · ")
    }
    readonly property var matrixChoices: {
        const keep = initial.matrix ?? ""
        if (keep === "")
            return [{ text: qsTr("Keep as is"), value: "" }]
        const out = [{ text: qsTr("Keep %1").arg(keep), value: "" }]
        const video = initial.videoMatrix ?? ""
        if (video !== "" && video !== keep)
            out.push({ text: qsTr("%1 (video)").arg(video), value: video })
        for (const m of initial.matrices ?? [])
            if (m !== keep && m !== video)
                out.push({ text: m, value: m })
        return out
    }
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
        app.resample(subsWidth.value, subsHeight.value, targetWidth.value, targetHeight.value,
                     shapeDiffers && aspect === "stretch", adjustRotations.checked, edges,
                     matrixChoices[matrix.currentIndex]?.value ?? "")
        close()
    }

    component EdgeBox: SpinBox {
        from: -10000; to: 10000; editable: true
        Layout.preferredWidth: 110
        onValueModified: dialog.edgeEdited()
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
                : qsTr("Scaled by %1 across and %2 down.").arg(dialog.scale(targetWidth.value / subsWidth.value),
                                                               dialog.scale(targetHeight.value / subsHeight.value))
        }
    }
    FormSection {
        objectName: "resampleAspectSection"
        title: qsTr("Aspect ratio")
        visible: dialog.shapeDiffers
        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            SegmentedButtons {
                objectName: "resampleAspect"
                choices: [{ text: qsTr("Keep it"), name: "resampleKeep" },
                          { text: qsTr("Stretch"), name: "resampleStretch" },
                          { text: qsTr("Letterbox"), name: "resampleLetterbox" },
                          { text: qsTr("Crop"), name: "resampleCrop" }]
                currentIndex: ["keep", "stretch", "letterbox", "crop"].indexOf(dialog.aspect)
                onActivated: i => dialog.aspect = ["keep", "stretch", "letterbox", "crop"][i]
            }
            Hint {
                text: qsTr("The aspect ratio changes from %1 to %2.").arg(dialog.ratio(subsWidth.value, subsHeight.value),
                                                                           dialog.ratio(targetWidth.value, targetHeight.value))
            }
            Hint {
                objectName: "resampleAspectHint"
                text: dialog.aspectHints[dialog.aspect]
            }
        }
    }
    Connections {
        target: subsWidth
        function onValueChanged() { dialog.fitEdges() }
    }
    Connections {
        target: subsHeight
        function onValueChanged() { dialog.fitEdges() }
    }
    Connections {
        target: targetWidth
        function onValueChanged() { dialog.fitEdges() }
    }
    Connections {
        target: targetHeight
        function onValueChanged() { dialog.fitEdges() }
    }
    FormSection {
        id: edgesSection
        objectName: "resampleEdges"
        title: qsTr("Edges")
        visible: dialog.shapeDiffers
        expanded: false
        summary: expanded ? "" : dialog.edgesSummary
        onToggled: open => expanded = open
        GridLayout {
            columns: 6
            columnSpacing: 6
            rowSpacing: 6
            Label { text: qsTr("Left") }
            EdgeBox { id: edgeLeft; objectName: "resampleEdgeLeft"; Accessible.name: qsTr("Left edge") }
            Label { text: qsTr("px"); color: Theme.muted }
            Label { text: qsTr("Right"); Layout.leftMargin: 12 }
            EdgeBox {
                id: edgeRight
                objectName: "resampleEdgeRight"
                enabled: !sameOnBothSides.checked
                Accessible.name: qsTr("Right edge")
            }
            Label { text: qsTr("px"); color: Theme.muted }
            Label { text: qsTr("Top") }
            EdgeBox { id: edgeTop; objectName: "resampleEdgeTop"; Accessible.name: qsTr("Top edge") }
            Label { text: qsTr("px"); color: Theme.muted }
            Label { text: qsTr("Bottom"); Layout.leftMargin: 12 }
            EdgeBox {
                id: edgeBottom
                objectName: "resampleEdgeBottom"
                enabled: !sameOnBothSides.checked
                Accessible.name: qsTr("Bottom edge")
            }
            Label { text: qsTr("px"); color: Theme.muted }
        }
        CheckBox {
            id: sameOnBothSides
            objectName: "resampleSameOnBothSides"
            text: qsTr("Same on both sides")
            checked: true
            onToggled: if (checked) dialog.edgeEdited()
        }
        Hint { text: qsTr("Added to (+) or cut from (−) each edge of the subtitles’ picture before it is scaled.") }
    }
    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 6
        Layout.fillWidth: true
        Label { text: qsTr("YCbCr matrix") }
        ComboBox {
            id: matrix
            objectName: "resampleMatrix"
            model: dialog.matrixChoices
            textRole: "text"
            enabled: (dialog.initial.matrix ?? "") !== ""
            Accessible.name: qsTr("YCbCr matrix")
        }
        Item { implicitWidth: 1 }
        Hint {
            objectName: "resampleMatrixHint"
            text: matrix.enabled ? qsTr("Colours are converted so that they look the same under the new matrix.")
                                 : qsTr("The subtitles have no YCbCr matrix to convert from.")
        }
    }
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        CheckBox {
            id: adjustRotations
            objectName: "resampleAdjustRotations"
            text: qsTr("Adjust 3D rotations")
            checked: true
        }
        Hint {
            Layout.leftMargin: adjustRotations.indicator.width + adjustRotations.spacing
            text: qsTr("Rotated lines keep their shape on screen.")
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
