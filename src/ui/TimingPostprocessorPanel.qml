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
    property bool exact: false
    property bool hasKeyframes: false
    property alias message: preview.message
    property alias plan: preview.plan
    signal apply()
    signal escaped()
    signal openVideo()
    signal openKeyframes()

    readonly property var settings: shiftTimes.settings
    readonly property int rules: settings.postprocessor
    readonly property bool wide: width >= 560
    readonly property size minimumBodySize: Qt.size(280, 160)

    function set(key, value) {
        const next = Object.assign({}, settings)
        next[key] = value
        shiftTimes.settings = next
    }
    function setRule(bit, on) {
        set("postprocessor", on ? (rules | bit) : (rules & ~bit))
    }
    function schedule() {
        refreshTimer.restart()
    }
    function refresh() {
        if (!visible)
            return
        exact = app.exactTimebase()
        hasKeyframes = app.hasKeyframes()
        preview.plan = app.shiftPreview("postprocessor")
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
            refresh()
            if (preview.changed > 0 && preview.problem === "")
                apply()
        }
    }
    Keys.onShortcutOverride: event => event.accepted = ownsKey(event)
    Keys.onPressed: event => handleKey(event)

    onSettingsChanged: refreshTimer.restart()
    onVisibleChanged: refreshTimer.restart()
    Timer {
        id: refreshTimer
        interval: 120
        onTriggered: panel.refresh()
    }

    component MsBox: SpinBox {
        property string key
        from: 0; to: 100000; stepSize: 10; editable: true
        value: panel.settings[key]
        Layout.preferredWidth: 110
        textFromValue: (value, locale) => qsTr("%1 ms").arg(value)
        valueFromText: (text, locale) => parseInt(text) || 0
        validator: RegularExpressionValidator { regularExpression: /\s*\d{0,6}\s*(ms)?\s*/ }
        onValueModified: panel.set(key, value)
    }
    component Sentence: Label {
        color: enabled ? Theme.text : Theme.disabled
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Rule: ColumnLayout {
        id: rule
        property string title
        property int bit
        property alias name: toggle.objectName
        default property alias body: content.data
        readonly property bool on: (panel.rules & bit) !== 0
        spacing: 6
        Layout.fillWidth: true
        Switch {
            id: toggle
            text: rule.title
            font.bold: true
            checked: rule.on
            Accessible.name: rule.title
            onToggled: panel.setRule(rule.bit, checked)
        }
        ColumnLayout {
            id: content
            enabled: rule.on
            spacing: 6
            Layout.fillWidth: true
            Layout.leftMargin: 24
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: panel.wide ? 3 : 1
        columnSpacing: 0
        rowSpacing: 0

        ScrollView {
            id: scroll
            objectName: "postprocessorScroll"
            Layout.fillWidth: !panel.wide
            Layout.preferredWidth: panel.wide ? 340 : -1
            Layout.fillHeight: panel.wide
            Layout.preferredHeight: form.implicitHeight + 16
            Layout.maximumHeight: panel.wide ? Number.POSITIVE_INFINITY : Math.max(160, panel.height - 200)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            contentHeight: form.implicitHeight + 16

            ColumnLayout {
                id: form
                x: 8
                y: 8
                width: scroll.availableWidth - 16
                spacing: 10

                ShiftPresetBar {
                    Layout.fillWidth: true
                    shiftTimes: panel.shiftTimes
                    tool: "postprocessor"
                    messages: panel.messages
                }
                ShiftNeed {
                    objectName: "postprocessorVideoNeed"
                    visible: !panel.exact
                    Layout.fillWidth: true
                    text: qsTr("The rules need a video.")
                    command: qsTr("Open video…")
                    onRun: panel.openVideo()
                }
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Lines")
                        color: Theme.muted
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 6
                    }
                    ShiftLines {
                        objectName: "postprocessorLines"
                        Layout.fillWidth: true
                        app: panel.app
                        whichLines: panel.settings.ppWhichLines
                        styles: panel.settings.ppStyles
                        onChose: (which, styles) => {
                            const next = Object.assign({}, panel.settings)
                            next.ppWhichLines = which
                            next.ppStyles = styles
                            panel.shiftTimes.settings = next
                        }
                    }
                }
                Rule {
                    name: "ppLeadIn"
                    title: qsTr("Lead-in")
                    bit: 1
                    RowLayout {
                        spacing: 8
                        Sentence { text: qsTr("Start earlier by") }
                        MsBox { key: "leadIn"; Accessible.name: qsTr("Start earlier by") }
                    }
                }
                Rule {
                    name: "ppLeadOut"
                    title: qsTr("Lead-out")
                    bit: 2
                    RowLayout {
                        spacing: 8
                        Sentence { text: qsTr("End later by") }
                        MsBox { key: "leadOut"; Accessible.name: qsTr("End later by") }
                    }
                }
                Rule {
                    id: gaps
                    name: "ppContinuous"
                    title: qsTr("Close small gaps")
                    bit: 4
                    RowLayout {
                        spacing: 8
                        Sentence { text: qsTr("Start back to the previous end if within") }
                        MsBox { key: "thresholdStart"; Accessible.name: qsTr("Start back to the previous end if within") }
                    }
                    RowLayout {
                        spacing: 8
                        Sentence { text: qsTr("End on to the next start if within") }
                        MsBox { key: "thresholdEnd"; Accessible.name: qsTr("End on to the next start if within") }
                    }
                    Label {
                        objectName: "ppGapsNeed"
                        visible: gaps.on && panel.settings.thresholdStart === 0 && panel.settings.thresholdEnd === 0
                        text: qsTr("Both gap limits are 0 ms. Raise one to close gaps.")
                        color: Theme.warning
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                Rule {
                    id: snap
                    name: "ppSnap"
                    title: qsTr("Snap to keyframes")
                    bit: 8
                    GridLayout {
                        columns: 3
                        columnSpacing: 8
                        rowSpacing: 6
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true; implicitHeight: 1 }
                        Label { text: qsTr("before"); color: Theme.muted }
                        Label { text: qsTr("after"); color: Theme.muted }
                        Sentence { text: qsTr("Start within") }
                        MsBox { key: "keyframeBeforeStart"; Accessible.name: qsTr("Start within, before") }
                        MsBox { key: "keyframeAfterStart"; Accessible.name: qsTr("Start within, after") }
                        Sentence { text: qsTr("End within") }
                        MsBox { key: "keyframeBeforeEnd"; Accessible.name: qsTr("End within, before") }
                        MsBox { key: "keyframeAfterEnd"; Accessible.name: qsTr("End within, after") }
                    }
                }
                ShiftNeed {
                    objectName: "ppSnapNeed"
                    visible: snap.on && panel.exact && !panel.hasKeyframes
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: qsTr("Needs keyframes.")
                    command: qsTr("Open keyframes…")
                    iconRole: "open-keyframes"
                    onRun: panel.openKeyframes()
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
            objectName: "postprocessorPreview"
            tool: "postprocessor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            onApply: panel.apply()
        }
    }
}
