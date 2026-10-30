pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: panel
    required property ShiftTimesController shiftTimes
    required property var app
    property bool hasVideo: false
    property bool audioHasMark: false
    property alias message: shiftMessage.text
    signal run()

    readonly property var settings: shiftTimes.settings
    readonly property bool postprocessorMode: (settings.postprocessor & 16) !== 0
    readonly property size minimumBodySize: Qt.size(shiftForm.implicitWidth + 16, footer.implicitHeight + 120)

    function set(key, value) {
        const next = Object.assign({}, settings)
        next[key] = value
        shiftTimes.settings = next
    }
    function setBit(key, bit, on) {
        set(key, on ? (settings[key] | bit) : (settings[key] & ~bit))
    }
    function collapsed(id) {
        return shiftTimes.collapsedSections.indexOf(id) >= 0
    }
    function setCollapsed(id, on) {
        const list = shiftTimes.collapsedSections.filter(s => s !== id)
        if (on)
            list.push(id)
        shiftTimes.collapsedSections = list
    }
    function timeText(ms) {
        const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
        const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
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
    // Set, not bound: the field's mouse steps assign its text, which would break a binding.
    onSettingsChanged: if (shiftTime.text !== amountText()) shiftTime.text = amountText()
    Component.onCompleted: shiftTime.text = amountText()

    component MsBox: SpinBox {
        property string key
        from: 0; to: 100000; stepSize: 10; editable: true
        value: panel.settings[key]
        Layout.fillWidth: true
        textFromValue: (value, locale) => value + " ms"
        valueFromText: (text, locale) => parseInt(text) || 0
        validator: RegularExpressionValidator { regularExpression: /\s*\d{0,6}\s*(ms)?\s*/ }
        onValueModified: panel.set(key, value)
    }
    component FieldLabel: Label {
        color: enabled ? Theme.muted : Theme.disabled
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component HalfColumn: ColumnLayout {
        spacing: 8
        Layout.fillWidth: true
        Layout.preferredWidth: 1 // equal shares with fillWidth
        Layout.alignment: Qt.AlignBottom
    }

    ScrollView {
        id: scroll
        objectName: "shiftScroll"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top }
        clip: true
        ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth
        contentHeight: shiftForm.implicitHeight + 16

        ColumnLayout {
            id: shiftForm
            x: 8
            y: 8
            width: scroll.availableWidth - 16
            spacing: 12

            SegmentedButtons {
                objectName: "shiftMode"
                Layout.fillWidth: true
                choices: [{text: qsTr("Shift"), name: "shiftModeShift"},
                          {text: qsTr("Timing postprocessor"), name: "shiftModePostprocessor"}]
                currentIndex: panel.postprocessorMode ? 1 : 0
                onActivated: index => panel.setBit("postprocessor", 16, index === 1)
            }

            ColumnLayout {
                visible: !panel.postprocessorMode
                spacing: 8
                Layout.fillWidth: true
                FieldLabel { text: qsTr("Profiles") }
                RowLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    ComboBox {
                        id: shiftProfile
                        objectName: "shiftProfile"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Profiles")
                        editable: true
                        model: panel.shiftTimes.profiles
                        onActivated: index => panel.shiftTimes.loadProfile(textAt(index))
                    }
                    IconToolButton {
                        objectName: "shiftProfileSave"
                        iconRole: "save"
                        text: qsTr("Adding and editing profiles")
                        onClicked: panel.shiftTimes.saveProfile(shiftProfile.editText)
                    }
                    IconToolButton {
                        objectName: "shiftProfileRemove"
                        iconRole: "remove"
                        text: qsTr("Removing profiles")
                        onClicked: panel.shiftTimes.removeProfile(shiftProfile.editText)
                    }
                }
            }

            FormSection {
                objectName: "shiftAmountSection"
                visible: !panel.postprocessorMode
                title: qsTr("Amount")
                expanded: !panel.collapsed("amount")
                onToggled: open => panel.setCollapsed("amount", !open)
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
                        Accessible.name: qsTr("Shift subtitle times")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Time shifts")
                        onTyped: panel.applyAmount(text)
                        onEditingFinished: panel.applyAmount(text)
                    }
                    SegmentedButtons {
                        objectName: "shiftUnit"
                        choices: [{text: qsTr("Time"), name: "shiftByTime", tip: qsTr("Shift times by time / frames")},
                                  {text: qsTr("Frames"), name: "shiftFrames", tip: qsTr("Shift times by time / frames"),
                                   enabled: panel.settings.byFrames || panel.app.exactTimebase()}]
                        currentIndex: panel.settings.byFrames ? 1 : 0
                        onActivated: index => panel.set("byFrames", index === 1)
                    }
                }
                SegmentedButtons {
                    objectName: "shiftDirection"
                    Layout.fillWidth: true
                    choices: [{text: qsTr("Forward"), name: "shiftForward", tip: qsTr("Delays subtitles")},
                              {text: qsTr("Backward"), name: "shiftBackward", tip: qsTr("Speeds up subtitles")}]
                    currentIndex: panel.settings.forward ? 0 : 1
                    onActivated: index => panel.set("forward", index === 0)
                }
                CheckBox {
                    objectName: "shiftTagTimes"
                    text: qsTr("Tag times")
                    checked: panel.settings.tagTimes
                    ToolTip.visible: hovered
                    // lupdate reads "\\" in a QML string as one backslash and keys the text wrongly.
                    ToolTip.text: qsTr("Shifts %1, %2, and %3 tag times\nso their positions on video do not change\n(slows down time shifting)")
                                      .arg("\\move", "\\t", "\\fad")
                    onToggled: panel.set("tagTimes", checked)
                }
            }

            FormSection {
                objectName: "shiftLinesSection"
                title: qsTr("Lines")
                expanded: !panel.collapsed("lines")
                onToggled: open => panel.setCollapsed("lines", !open)
                FieldLabel { text: qsTr("Which lines") }
                ComboBox {
                    objectName: "shiftWhichLines"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Which lines")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("The choice of lines to move")
                    model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From the selected line"),
                            qsTr("All times higher and equal"), qsTr("All times lower and equal"),
                            qsTr("According to the selected styles")]
                    currentIndex: panel.settings.whichLines
                    onActivated: index => panel.set("whichLines", index)
                }
                RowLayout {
                    visible: panel.settings.whichLines === 5
                    spacing: 4
                    Layout.fillWidth: true
                    IconToolButton {
                        id: stylesPick
                        objectName: "shiftStylesPick"
                        iconRole: "add"
                        text: qsTr("Select a style from the list")
                        onClicked: {
                            stylesPopup.before = panel.settings
                            stylesPopup.openFor(stylesPick, panel.app.styleNames(),
                                                (panel.settings.styles || "").split(",").map(n => n.trim()),
                                                { counts: panel.app.styleLineCounts() })
                        }
                    }
                    TextField {
                        id: shiftStyles
                        objectName: "shiftStyles"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Styles, separated by commas")
                        Accessible.name: qsTr("Styles")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Move according to the following styles (separated by a comma)")
                        text: panel.settings.styles
                        onEditingFinished: if (text !== panel.settings.styles) panel.set("styles", text)
                    }
                }
                FieldLabel { visible: !panel.postprocessorMode; text: qsTr("A method of time shift") }
                ComboBox {
                    objectName: "shiftWhichTimes"
                    visible: !panel.postprocessorMode
                    Layout.fillWidth: true
                    Accessible.name: qsTr("A method of time shift")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("The choice of times to move")
                    model: [qsTr("Both times"), qsTr("The starting time"), qsTr("End time")]
                    currentIndex: panel.settings.whichTimes
                    onActivated: index => panel.set("whichTimes", index)
                }
                FieldLabel { visible: !panel.postprocessorMode; text: qsTr("End time correction") }
                ComboBox {
                    objectName: "shiftEndCorrection"
                    visible: !panel.postprocessorMode
                    Layout.fillWidth: true
                    Accessible.name: qsTr("End time correction")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Corrects end times that are improper or overlap each other")
                    model: [qsTr("Leave unchanged"), qsTr("Adjust overlapping times"), qsTr("New times")]
                    currentIndex: panel.settings.correctEndTimes
                    onActivated: index => panel.set("correctEndTimes", index)
                }
            }

            FormSection {
                objectName: "shiftVideoAudioSection"
                visible: !panel.postprocessorMode
                title: qsTr("Shift by video / audio")
                expanded: !panel.collapsed("videoAudio")
                onToggled: open => panel.setCollapsed("videoAudio", !open)
                SegmentedButtons {
                    objectName: "shiftVideoAudioTime"
                    Layout.fillWidth: true
                    choices: [{text: qsTr("Beginning"), name: "shiftFromStart", tip: qsTr("Moves the start time to the video/audio time")},
                              {text: qsTr("End"), name: "shiftFromEnd", tip: qsTr("Moves the end time to the video/audio time")}]
                    currentIndex: panel.settings.fromStartTime ? 0 : 1
                    onActivated: index => panel.set("fromStartTime", index === 0)
                }
                CheckBox {
                    objectName: "shiftToVideo"
                    text: qsTr("Move the marker to video time")
                    enabled: panel.hasVideo
                    checked: panel.settings.moveToVideoTime
                    onToggled: panel.set("moveToVideoTime", checked)
                }
                CheckBox {
                    objectName: "shiftToAudio"
                    text: qsTr("Move the marker to audio time")
                    enabled: panel.audioHasMark
                    checked: panel.settings.moveToAudioTime
                    onToggled: panel.set("moveToAudioTime", checked)
                }
            }

            FormSection {
                objectName: "ppLeadSection"
                visible: panel.postprocessorMode
                title: qsTr("Lead-in/lead-out")
                expanded: !panel.collapsed("leadInOut")
                onToggled: open => panel.setCollapsed("leadInOut", !open)
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    HalfColumn {
                        CheckBox {
                            id: ppLeadIn
                            objectName: "ppLeadIn"
                            text: qsTr("Lead-in")
                            checked: (panel.settings.postprocessor & 1) !== 0
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Inserts introduction to the start time (good when using fad)")
                            onToggled: panel.setBit("postprocessor", 1, checked)
                        }
                        MsBox {
                            key: "leadIn"
                            enabled: ppLeadIn.checked
                            Accessible.name: qsTr("Lead-in")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Introduction time in milliseconds")
                        }
                    }
                    HalfColumn {
                        CheckBox {
                            id: ppLeadOut
                            objectName: "ppLeadOut"
                            text: qsTr("Lead-out")
                            checked: (panel.settings.postprocessor & 2) !== 0
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Inserts introduction to the end time (good when using fad)")
                            onToggled: panel.setBit("postprocessor", 2, checked)
                        }
                        MsBox {
                            key: "leadOut"
                            enabled: ppLeadOut.checked
                            Accessible.name: qsTr("Lead-out")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Lead-out time in milliseconds")
                        }
                    }
                }
            }

            FormSection {
                objectName: "ppContinuousSection"
                visible: panel.postprocessorMode
                title: qsTr("Set times as continuous")
                expanded: !panel.collapsed("continuous")
                onToggled: open => panel.setCollapsed("continuous", !open)
                CheckBox {
                    id: ppContinuous
                    objectName: "ppContinuous"
                    text: qsTr("Enable")
                    Accessible.name: qsTr("Set times as continuous")
                    checked: (panel.settings.postprocessor & 4) !== 0
                    onToggled: panel.setBit("postprocessor", 4, checked)
                }
                RowLayout {
                    spacing: 8
                    enabled: ppContinuous.checked
                    Layout.fillWidth: true
                    HalfColumn {
                        FieldLabel { text: qsTr("Start time threshold") }
                        MsBox {
                            key: "thresholdStart"
                            Accessible.name: qsTr("Start time threshold")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Start time extension threshold")
                        }
                    }
                    HalfColumn {
                        FieldLabel { text: qsTr("End time threshold") }
                        MsBox {
                            key: "thresholdEnd"
                            Accessible.name: qsTr("End time threshold")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("End time extension threshold")
                        }
                    }
                }
            }

            FormSection {
                objectName: "ppSnapSection"
                visible: panel.postprocessorMode
                title: qsTr("Snap to keyframes")
                expanded: !panel.collapsed("snap")
                onToggled: open => panel.setCollapsed("snap", !open)
                CheckBox {
                    id: ppSnap
                    objectName: "ppSnap"
                    text: qsTr("Enable")
                    Accessible.name: qsTr("Snap to keyframes")
                    enabled: panel.app.exactTimebase()
                    checked: (panel.settings.postprocessor & 8) !== 0
                    onToggled: panel.setBit("postprocessor", 8, checked)
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    enabled: ppSnap.checked
                    Layout.fillWidth: true
                    HalfColumn {
                        FieldLabel { text: qsTr("Before the start time") }
                        MsBox {
                            key: "keyframeBeforeStart"
                            Accessible.name: qsTr("Before the start time")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The maximum shift to a keyframe \nbefore the start time in milliseconds")
                        }
                    }
                    HalfColumn {
                        FieldLabel { text: qsTr("After the start time") }
                        MsBox {
                            key: "keyframeAfterStart"
                            Accessible.name: qsTr("After the start time")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The maximum shift to a keyframe \nafter the start time in milliseconds")
                        }
                    }
                    HalfColumn {
                        FieldLabel { text: qsTr("Before the end time") }
                        MsBox {
                            key: "keyframeBeforeEnd"
                            Accessible.name: qsTr("Before the end time")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The maximum shift to a keyframe \nbefore the end time in milliseconds")
                        }
                    }
                    HalfColumn {
                        FieldLabel { text: qsTr("After the end time") }
                        MsBox {
                            key: "keyframeAfterEnd"
                            Accessible.name: qsTr("After the end time")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The maximum shift to a keyframe \nafter the end time in milliseconds")
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 8
        Rectangle {
            color: Theme.line
            implicitHeight: 1
            Layout.fillWidth: true
            Layout.bottomMargin: 4
        }
        Label {
            id: shiftMessage
            objectName: "shiftMessage"
            visible: text.length > 0
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Accessible.role: Accessible.AlertMessage
        }
        PrimaryButton {
            objectName: "shiftApply"
            text: panel.postprocessorMode ? qsTr("Run timing postprocessor") : qsTr("Shift")
            Accessible.description: panel.postprocessorMode ? qsTr("Run timing postprocessor") : qsTr("Shift subtitle times")
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            onClicked: panel.run()
        }
    }

    ChooseStyles {
        id: stylesPopup
        objectName: "shiftStylesPopup"
        property var before: null
        onChanged: (names, reverted) => {
            if (reverted && before) {
                panel.shiftTimes.settings = before
                return
            }
            const next = Object.assign({}, panel.settings)
            next.styles = names.join(",")
            if (next.styles.length > 0)
                next.whichLines = 5
            panel.shiftTimes.settings = next
        }
    }
}
