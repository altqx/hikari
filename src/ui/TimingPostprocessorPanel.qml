pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property ShiftTimesController shiftTimes
    required property var app
    required property int shellWindow
    property var messages: null
    property bool exact: false
    property bool hasKeyframes: false
    property bool assistant: false
    property alias message: preview.message
    property alias plan: preview.plan
    signal apply()
    signal escaped()
    signal openVideo()
    signal openKeyframes()

    readonly property var settings: shiftTimes.settings
    readonly property int rules: settings.postprocessor
    property bool showingChanges: false
    readonly property size minimumBodySize: Qt.size(280, 160)
    readonly property int changed: preview.changed
    readonly property var firstChanges: (preview.problem === "" ? (preview.plan.rows || []) : []).filter(row => row.changed).slice(0, 3)

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
        preview.plan = app.shiftPreview("postprocessor", shellWindow)
        if (assistant)
            assistantView.refresh()
    }
    function backToRules() {
        assistant = false
        showingChanges = false
    }
    function focusAssistant() {
        assistantView.restart()
        assistantView.forceActiveFocus()
    }
    function ownsKey(event) {
        if (assistant && assistantView.ownsKey(event))
            return true
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
        return (enter && mods === Qt.ControlModifier) || (event.key === Qt.Key_Escape && mods === 0)
    }
    function handleKey(event) {
        if (!ownsKey(event))
            return
        if (assistant && assistantView.handleKey(event))
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape) {
            if (assistant || showingChanges)
                backToRules()
            else
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
    onAssistantChanged: if (assistant) focusAssistant()
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
        CheckBox {
            id: toggle
            text: rule.title
            leftPadding: 0
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

    component PageHeader: RowLayout {
        property alias title: heading.text
        property alias backName: back.objectName
        spacing: 6
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 12
        Layout.topMargin: 8
        IconToolButton {
            id: back
            iconRole: "chevron-left"
            text: qsTr("Back")
            onClicked: panel.backToRules()
        }
        Label {
            id: heading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            objectName: "postprocessorPages"
            currentIndex: panel.assistant ? 2 : panel.showingChanges ? 1 : 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                id: scroll
                objectName: "postprocessorScroll"
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth
                contentHeight: form.implicitHeight + 24

                ColumnLayout {
                    id: form
                    x: 12
                    y: 12
                    width: scroll.availableWidth - 24
                    spacing: 12

                    ShiftPresetBar {
                        Layout.fillWidth: true
                        shiftTimes: panel.shiftTimes
                        tool: "postprocessor"
                        messages: panel.messages
                        appSettings: panel.app ? panel.app.settings : null
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
                            Layout.topMargin: 4
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
                        RowLayout {
                            spacing: 8
                            Sentence { text: qsTr("Join lines that overlap by up to") }
                            MsBox { key: "maxOverlap"; Accessible.name: qsTr("Join lines that overlap by up to") }
                        }
                        Label {
                            objectName: "ppGapsNeed"
                            visible: gaps.on && panel.settings.thresholdStart === 0 && panel.settings.thresholdEnd === 0
                                     && panel.settings.maxOverlap === 0
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
                    Rectangle {
                        color: Theme.line
                        implicitHeight: 1
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Label {
                            objectName: "postprocessorChangeCount"
                            text: preview.problem !== "" ? preview.problem
                                  : panel.changed === 0 ? qsTr("No line changes")
                                  : qsTr("%n line(s) change", "", panel.changed)
                            font.weight: Font.DemiBold
                            color: preview.problem !== "" ? Theme.danger : Theme.text
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        LinkButton {
                            objectName: "postprocessorShowChanges"
                            visible: panel.changed > 0
                            text: qsTr("Show changes")
                            onClicked: panel.showingChanges = true
                        }
                    }
                    Repeater {
                        model: panel.firstChanges
                        delegate: Label {
                            required property var modelData
                            readonly property bool startMoved: modelData.start !== modelData.startBefore
                            readonly property bool snapped: modelData.rules.indexOf(startMoved ? "snappedStart" : "snappedEnd") >= 0
                            objectName: "postprocessorChange"
                            text: modelData.line + "  "
                                  + preview.timeText(startMoved ? modelData.startBefore : modelData.endBefore) + " → "
                                  + (snapped ? qsTr("kf") : preview.timeText(startMoved ? modelData.start : modelData.end))
                            font.family: "monospace"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            ColumnLayout {
                spacing: 0
                PageHeader { title: qsTr("Changes"); backName: "postprocessorBack" }
                ShiftPreview {
                    id: preview
                    objectName: "postprocessorPreview"
                    tool: "postprocessor"
                    showsFooter: false
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
            ColumnLayout {
                spacing: 0
                PageHeader { title: qsTr("Timing assistant"); backName: "timingAssistantBack" }
                TimingAssistantView {
                    id: assistantView
                    objectName: "timingAssistant"
                    app: panel.app
                    shellWindow: panel.shellWindow
                    narrow: panel.width < 560
                    Layout.margins: 12
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
        Rectangle {
            color: Theme.line
            implicitHeight: 1
            Layout.fillWidth: true
        }
        Label {
            objectName: "postprocessorMessage"
            visible: preview.message.length > 0
            text: preview.message
            color: Theme.danger
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Layout.margins: 12
            Label {
                text: qsTr("One undo step: “%1”").arg(preview.stepName)
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            PrimaryButton {
                objectName: "postprocessorApply"
                text: qsTr("Apply")
                enabled: preview.problem === "" && panel.changed > 0
                onClicked: panel.apply()
            }
        }
    }
}
