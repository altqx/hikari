pragma Translator: "HikariSub.Audio"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// KA2 (#277, KA2-tap-to-time): the tap bar above the audio display while tap
// mode is on. Its state by a shape and a word (● Tapping, ■ Stopped,
// ○ Ready), the Line's number and syllables (tapped ones muted, the held one
// filled, the next one outlined), the next Line's text and the last note;
// at the right what the keys or buttons do in that state: Backspace undoes
// and Esc stops while tapping, Resume and Revert Line once stopped, Auto
// split while the Line is one syllable.
Rectangle {
    id: bar
    objectName: "audioTapBar"
    required property var audio
    readonly property int tapState: audio.tapState
    readonly property bool tapping: tapState === AudioController.TapTapping
    readonly property bool stopped: tapState === AudioController.TapStopped
    readonly property bool needsSplit: tapState === AudioController.TapReady && audio.tapNeedsSplit
    implicitHeight: 30
    color: Theme.panel
    clip: true

    // A button of the bar; `primary` fills it in the accent, as the
    // default action. The keys stay tap mode's: it takes no focus.
    component BarButton: AbstractButton {
        id: barButton
        property bool primary: false
        property string iconRole
        property string key
        focusPolicy: Qt.NoFocus
        implicitHeight: 22
        implicitWidth: buttonRow.implicitWidth + 16
        Accessible.name: text
        readonly property color ink: primary ? Theme.onAccent : Theme.text
        background: Rectangle {
            radius: 3
            color: barButton.primary
                   ? (barButton.down ? Qt.darker(Theme.accent, Theme.dark ? 1.25 : 1.35)
                      : barButton.hovered ? Qt.darker(Theme.accent, Theme.dark ? 1.1 : 1.15) : Theme.accent)
                   : barButton.down ? Theme.select : barButton.hovered ? Theme.field : Theme.raised
            border.color: barButton.primary ? Theme.accent : barButton.hovered ? Theme.text : Theme.control
        }
        contentItem: Item {
            RowLayout {
                id: buttonRow
                anchors.centerIn: parent
                spacing: 6
                Icon {
                    visible: barButton.iconRole.length > 0
                    iconRole: barButton.iconRole
                    color: barButton.ink
                    accentColor: barButton.ink
                }
                Label {
                    text: barButton.text
                    color: barButton.ink
                    font.bold: barButton.primary
                }
                Label {
                    visible: barButton.key.length > 0
                    text: barButton.key
                    color: barButton.ink
                    opacity: 0.85
                    font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.8)
                }
            }
        }
    }
    // A key and what it does ("⌫ Undo").
    component KeyHint: RowLayout {
        property alias key: cap.text
        property alias text: hintLabel.text
        spacing: 4
        Keycap {
            id: cap
        }
        Label {
            id: hintLabel
            color: Theme.muted
            font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.line
    }
    RowLayout {
        anchors { fill: parent; leftMargin: 8; rightMargin: 8; bottomMargin: 1 }
        spacing: 10
        // the state: its shape and its word
        RowLayout {
            spacing: 6
            Item {
                implicitWidth: 9
                implicitHeight: 9
                // ● Tapping: a red dot
                Rectangle {
                    anchors.fill: parent
                    radius: 4.5
                    color: Theme.danger
                    visible: bar.tapping
                }
                // ■ Stopped: a square
                Rectangle {
                    anchors.fill: parent
                    color: Theme.text
                    visible: bar.stopped
                }
                // ○ Ready: a ring
                Rectangle {
                    anchors.fill: parent
                    radius: 4.5
                    color: "transparent"
                    border.width: 1.5
                    border.color: Theme.text
                    visible: !bar.tapping && !bar.stopped
                }
            }
            Label {
                objectName: "audioTapState"
                font.bold: true
                text: bar.tapping ? qsTr("Tapping") : bar.stopped ? qsTr("Stopped") : qsTr("Ready")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
        // the Line, its syllables and the next Line, cut at the right
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitWidth: lyrics.implicitWidth
            clip: true
            RowLayout {
                id: lyrics
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Label {
                    objectName: "audioTapLine"
                    //: The tap bar; %1 is the Line's row number
                    text: qsTr("Line %1").arg(bar.audio.tapLine)
                    color: Theme.muted
                    rightPadding: 4
                }
                Repeater {
                    objectName: "audioTapSyllables"
                    model: bar.audio.tapSyllables
                    delegate: Label {
                        required property var modelData
                        required property int index
                        readonly property bool tapped: modelData.state === AudioController.TapSyllableTapped
                        readonly property bool held: modelData.state === AudioController.TapSyllableHeld
                        readonly property bool isNext: modelData.state === AudioController.TapSyllableNext
                        objectName: "audioTapSyllable" + index
                        text: modelData.text
                        leftPadding: 5
                        rightPadding: 5
                        topPadding: 1
                        bottomPadding: 1
                        font.bold: true
                        // tapped: muted (underlined in high contrast, not by its colour alone)
                        font.underline: tapped && Theme.highContrast
                        color: held ? Theme.onAccent : tapped ? Theme.muted : Theme.text
                        background: Rectangle {
                            radius: 3
                            color: parent.held ? Theme.accent : "transparent"
                            border.color: parent.isNext || parent.held ? Theme.accent : "transparent"
                        }
                    }
                }
                Label {
                    objectName: "audioTapNext"
                    visible: bar.audio.tapNextText.length > 0 && !bar.stopped && !bar.needsSplit
                    text: "› " + bar.audio.tapNextText
                    color: Theme.muted
                    leftPadding: 6
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "audioTapMessage"
                    visible: text.length > 0
                    text: bar.needsSplit ? qsTr("This Line is one syllable. Split it first, then tap.") : bar.audio.tapMessage
                    font.bold: true
                    leftPadding: 6
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
        // what the keys and buttons do now
        KeyHint {
            visible: bar.tapping
            key: "⌫"
            text: qsTr("Undo")
        }
        KeyHint {
            visible: bar.tapping
            key: qsTr("Esc")
            text: qsTr("Stop")
        }
        BarButton {
            objectName: "audioTapResume"
            visible: bar.stopped
            primary: true
            text: qsTr("Resume")
            key: qsTr("Space")
            onClicked: bar.audio.tapResume()
        }
        BarButton {
            objectName: "audioTapRevert"
            visible: bar.stopped
            text: qsTr("Revert Line")
            onClicked: bar.audio.tapRevert()
        }
        BarButton {
            objectName: "audioTapAutoSplit"
            visible: bar.needsSplit
            primary: true
            iconRole: "karaoke-split"
            text: qsTr("Auto split")
            onClicked: bar.audio.tapAutoSplit()
        }
        KeyHint {
            visible: bar.tapState === AudioController.TapReady && !bar.needsSplit
            key: qsTr("Space")
            text: qsTr("Start")
        }
    }
}
