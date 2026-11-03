pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property ShiftTimesController shiftTimes
    required property var app
    property var messages: null
    property bool hasVideo: false
    property bool audioHasMark: false
    property bool exact: false
    property alias message: preview.message
    property alias plan: preview.plan
    signal apply()
    signal escaped()
    signal openVideo()

    readonly property var settings: shiftTimes.settings
    readonly property bool wide: width >= 640
    readonly property int shiftBy: settings.moveToVideoTime ? 1 : settings.moveToAudioTime ? 2 : 0
    readonly property size minimumBodySize: Qt.size(280, 160)

    function set(key, value) {
        const next = Object.assign({}, settings)
        next[key] = value
        shiftTimes.settings = next
    }
    function setShiftBy(index) {
        const next = Object.assign({}, settings)
        next.moveToVideoTime = index === 1
        next.moveToAudioTime = index === 2
        shiftTimes.settings = next
    }
    function schedule() {
        refreshTimer.restart()
    }
    function refresh() {
        if (!visible)
            return
        exact = app.exactTimebase()
        preview.plan = app.shiftPreview("shift")
    }
    function timeText(ms) {
        return preview.timeText(ms)
    }
    function parseTime(text) {
        const m = /^(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?$/.exec(text.trim())
        if (!m)
            return -1
        return ((Number(m[1]) * 60 + Number(m[2])) * 60 + Number(m[3])) * 1000 + (m[4] ? Number(m[4].padEnd(2, "0")) * 10 : 0)
    }
    function amountText() {
        return settings.byFrames ? String(settings.frames) : timeText(settings.timeMs)
    }
    function applyAmount(text) {
        if (settings.byFrames) {
            const frames = Math.max(0, parseInt(text) || 0)
            if (frames !== settings.frames)
                set("frames", frames)
        } else {
            const ms = parseTime(text)
            if (ms >= 0 && ms !== settings.timeMs)
                set("timeMs", ms)
        }
    }
    function ownsKey(event) {
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
        return (enter && mods === Qt.ControlModifier) || (event.key === Qt.Key_Escape && mods === 0)
    }
    function handleKey(event) {
        if (!ownsKey(event))
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape) {
            escaped()
        } else {
            shiftTime.editingFinished()
            if (preview.changed > 0 && preview.problem === "")
                apply()
        }
    }
    Keys.onShortcutOverride: event => event.accepted = ownsKey(event)
    Keys.onPressed: event => handleKey(event)

    // Set, not bound: the field's mouse steps assign its text, which would break a binding.
    onSettingsChanged: {
        if (shiftTime.text !== amountText())
            shiftTime.text = amountText()
        refreshTimer.restart()
    }
    onVisibleChanged: refreshTimer.restart()
    Component.onCompleted: shiftTime.text = amountText()
    Timer {
        id: refreshTimer
        interval: 120
        onTriggered: panel.refresh()
    }

    component FieldLabel: Label {
        color: enabled ? Theme.muted : Theme.disabled
        wrapMode: Text.WordWrap
        Layout.alignment: Qt.AlignVCenter
    }

    GridLayout {
        anchors.fill: parent
        columns: panel.wide ? 3 : 1
        columnSpacing: 0
        rowSpacing: 0

        ScrollView {
            id: scroll
            objectName: "shiftScroll"
            Layout.fillWidth: !panel.wide
            Layout.preferredWidth: panel.wide ? 420 : -1
            Layout.fillHeight: panel.wide
            Layout.preferredHeight: shiftForm.implicitHeight + 16
            Layout.maximumHeight: panel.wide ? Number.POSITIVE_INFINITY : Math.max(160, panel.height - 200)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            contentHeight: shiftForm.implicitHeight + 16

            ColumnLayout {
                id: shiftForm
                x: 8
                y: 8
                width: scroll.availableWidth - 16
                spacing: 8

                ShiftPresetBar {
                    Layout.fillWidth: true
                    shiftTimes: panel.shiftTimes
                    tool: "shift"
                    messages: panel.messages
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true

                    FieldLabel { text: qsTr("Shift by") }
                    SegmentedButtons {
                        objectName: "shiftBy"
                        Accessible.name: qsTr("Shift by")
                        Layout.fillWidth: true
                        choices: [{text: qsTr("Amount"), name: "shiftByAmount"},
                                  {text: qsTr("To video"), name: "shiftByVideo", enabled: panel.hasVideo || panel.shiftBy === 1,
                                   tip: qsTr("Moves the active line to the video’s frame")},
                                  {text: qsTr("To audio mark"), name: "shiftByAudio", enabled: panel.audioHasMark || panel.shiftBy === 2,
                                   tip: qsTr("Moves the active line to the audio mark")}]
                        currentIndex: panel.shiftBy
                        onActivated: index => panel.setShiftBy(index)
                    }

                    FieldLabel { visible: panel.shiftBy !== 0; text: qsTr("Move") }
                    SegmentedButtons {
                        objectName: "shiftMoveWhich"
                        visible: panel.shiftBy !== 0
                        Accessible.name: qsTr("Move the active line’s")
                        choices: [{text: qsTr("Start"), name: "shiftMoveStart"}, {text: qsTr("End"), name: "shiftMoveEnd"}]
                        currentIndex: panel.settings.fromStartTime ? 0 : 1
                        onActivated: index => panel.set("fromStartTime", index === 0)
                    }
                    Item { visible: panel.shiftBy !== 0 && (preview.plan.targetMs ?? -1) >= 0; implicitWidth: 1 }
                    Label {
                        objectName: "shiftTarget"
                        visible: panel.shiftBy !== 0 && (preview.plan.targetMs ?? -1) >= 0
                        text: (panel.shiftBy === 1 ? qsTr("To the video at %1") : qsTr("To the audio mark at %1"))
                                  .arg(panel.timeText(preview.plan.targetMs ?? 0))
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    FieldLabel { text: panel.shiftBy === 0 ? qsTr("Amount") : qsTr("Plus") }
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        TimeField {
                            id: shiftTime
                            objectName: "shiftTime"
                            Layout.fillWidth: true
                            form: panel.settings.byFrames ? 3 : 0
                            mouseForm: 0
                            frames: panel.settings.byFrames
                            Accessible.name: panel.shiftBy === 0 ? qsTr("Amount") : qsTr("Plus")
                            onTyped: panel.applyAmount(text)
                            onEditingFinished: panel.applyAmount(text)
                        }
                        SegmentedButtons {
                            objectName: "shiftUnit"
                            Accessible.name: qsTr("Unit")
                            choices: [{text: qsTr("Time"), name: "shiftByTime"},
                                      {text: qsTr("Frames"), name: "shiftFrames"}]
                            currentIndex: panel.settings.byFrames ? 1 : 0
                            onActivated: index => panel.set("byFrames", index === 1)
                        }
                    }
                    Item { visible: framesNeed.visible; implicitWidth: 1 }
                    ShiftNeed {
                        id: framesNeed
                        objectName: "shiftFramesNeed"
                        visible: panel.settings.byFrames && !panel.exact
                        Layout.fillWidth: true
                        text: qsTr("Frames need a video.")
                        command: qsTr("Open video…")
                        onRun: panel.openVideo()
                    }

                    FieldLabel { text: qsTr("Direction") }
                    SegmentedButtons {
                        objectName: "shiftDirection"
                        Accessible.name: qsTr("Direction")
                        choices: [{text: qsTr("Later"), name: "shiftForward"}, {text: qsTr("Earlier"), name: "shiftBackward"}]
                        currentIndex: panel.settings.forward ? 0 : 1
                        onActivated: index => panel.set("forward", index === 0)
                    }

                    FieldLabel { text: qsTr("Lines"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
                    ShiftLines {
                        objectName: "shiftLines"
                        Layout.fillWidth: true
                        app: panel.app
                        whichLines: panel.settings.whichLines
                        styles: panel.settings.styles
                        onChose: (which, styles) => {
                            const next = Object.assign({}, panel.settings)
                            next.whichLines = which
                            next.styles = styles
                            panel.shiftTimes.settings = next
                        }
                    }

                    FieldLabel { text: qsTr("Times") }
                    SegmentedButtons {
                        objectName: "shiftWhichTimes"
                        Accessible.name: qsTr("Times")
                        choices: [{text: qsTr("Both"), name: "shiftBothTimes"}, {text: qsTr("Start"), name: "shiftStartTimes"},
                                  {text: qsTr("End"), name: "shiftEndTimes"}]
                        currentIndex: panel.settings.whichTimes
                        onActivated: index => panel.set("whichTimes", index)
                    }

                    FieldLabel { text: qsTr("End times") }
                    ComboBox {
                        objectName: "shiftEndCorrection"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("End times")
                        model: [qsTr("Leave as they are"), qsTr("Fix overlaps"), qsTr("Recalculate from text length")]
                        currentIndex: panel.settings.correctEndTimes
                        onActivated: index => panel.set("correctEndTimes", index)
                    }
                    Item { visible: endNeed.visible; implicitWidth: 1 }
                    ShiftNeed {
                        id: endNeed
                        objectName: "shiftEndNeed"
                        visible: panel.settings.correctEndTimes > 0 && !panel.exact
                        Layout.fillWidth: true
                        text: qsTr("Needs a video.")
                        command: qsTr("Open video…")
                        onRun: panel.openVideo()
                    }
                }
                CheckBox {
                    objectName: "shiftTagTimes"
                    // lupdate reads "\\" in a QML string as one backslash and keys the text wrongly.
                    text: qsTr("Move tag times too (%1, %2, %3)").arg("\\move", "\\t", "\\fad")
                    checked: panel.settings.tagTimes
                    onToggled: panel.set("tagTimes", checked)
                }
                CheckBox {
                    objectName: "shiftCenterTimes"
                    text: qsTr("Centre times within frames")
                    checked: panel.settings.centerTimes
                    onToggled: panel.set("centerTimes", checked)
                }
                ShiftNeed {
                    objectName: "shiftCenterNeed"
                    visible: panel.settings.centerTimes && !panel.exact
                    Layout.fillWidth: true
                    text: qsTr("Needs a video.")
                    command: qsTr("Open video…")
                    onRun: panel.openVideo()
                }
            }
        }
        Rectangle {
            visible: panel.wide
            color: Theme.line
            Layout.preferredWidth: 1
            Layout.fillHeight: true
        }
        ShiftPreview {
            id: preview
            objectName: "shiftPreview"
            tool: "shift"
            Layout.fillWidth: true
            Layout.fillHeight: true
            onApply: panel.apply()
        }
    }
}
