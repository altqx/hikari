pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

GridLayout {
    id: discord
    objectName: "discordSettings"
    required property var dialog
    readonly property var app: dialog.app
    readonly property var values: dialog.values
    readonly property bool on: values["privacy.discordPresence"] === true
    readonly property bool wide: width >= 760
    readonly property var preview: app ? app.presencePreview(values) : ({})
    readonly property var placeholders: ["mode", "file", "project", "show", "episode", "title", "plugin"]
    readonly property var modeColours: ({
        editing: "#8FD3FF", timing: "#7EE0C3", translation: "#B9A6FF", typesetting: "#FBD46D", karaoke: "#FFA8A0",
        watching: "#C9E7A4", effects: "#A6E3E9", styling: "#FFD1A8", spelling: "#E6D6B8", reviewing: "#FFC6E0",
        exporting: "#C5CAE9", agent: "#F6B8F0"
    })
    readonly property var modeIcons: ({
        editing: "editor", timing: "audio-line-mode", translation: "translation-mode", typesetting: "tool-position",
        karaoke: "karaoke", watching: "media-play", effects: "node-editor", styling: "styles", spelling: "spellchecker",
        reviewing: "qc-notes", exporting: "export", agent: "ai-assistant"
    })
    property TextField lastLine: null

    columns: wide ? 2 : 1
    columnSpacing: 24
    rowSpacing: 12
    Layout.fillWidth: true

    function put(setting, value) { dialog.put(setting, value) }
    function offSwitch(name) {
        if (name === "mode" && values["privacy.discordMode"] !== true)
            return qsTr("The mode")
        if ((name === "file" || name === "episode") && values["privacy.discordFileName"] !== true)
            return qsTr("The file name")
        if (name === "project" && values["privacy.discordProjectName"] !== true)
            return qsTr("The project’s name")
        return ""
    }
    function emptyNotes(line) {
        return placeholders.filter(p => (line ?? "").includes("{" + p + "}") && offSwitch(p) !== "")
            .map(p => qsTr("{%1} stays empty while %2 is off.").arg(p, offSwitch(p)))
    }
    function validLink(url) { return /^https:\/\/[^\s\/]+\.[^\s]+/.test(url) }
    function toggleActivity(key, ticked) {
        const now = (values["privacy.discordActivities"] ?? []).filter(k => k !== key)
        put("privacy.discordActivities", ticked ? now.concat([key]) : now)
    }

    component Check: CheckBox {
        id: check
        required property string setting
        objectName: "setting_" + setting
        enabled: discord.on
        leftPadding: 1
        spacing: 9
        Layout.fillWidth: true
        Accessible.name: text
        checked: discord.values[setting] === true
        onToggled: discord.put(setting, checked)
    }
    component Choice: ComboBox {
        id: choice
        required property string setting
        required property var options
        objectName: "setting_" + setting
        enabled: discord.on
        textRole: "text"
        valueRole: "value"
        model: options
        Layout.preferredWidth: 260
        currentIndex: Math.max(0, options.findIndex(o => o.value === (discord.values[setting] ?? "")))
        onActivated: index => discord.put(setting, options[index].value)
    }
    component Note: Label {
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    component Line: ColumnLayout {
        id: line
        required property string setting
        required property string name
        readonly property string value: discord.values[setting] ?? ""
        spacing: 4
        Label { text: line.name; color: Theme.text }
        RowLayout {
            spacing: 8
            TextField {
                id: field
                objectName: "setting_" + line.setting
                enabled: discord.on
                text: line.value
                maximumLength: 512
                Layout.preferredWidth: 420
                Accessible.name: line.name
                onTextEdited: discord.put(line.setting, text)
                onActiveFocusChanged: if (activeFocus) discord.lastLine = field
            }
            Label {
                objectName: "discordCounter_" + line.setting
                text: qsTr("%1/128").arg(line.value.length)
                color: line.value.length > 128 ? Theme.danger : Theme.muted
            }
        }
        Repeater {
            model: discord.emptyNotes(line.value)
            Note { required property string modelData; text: modelData }
        }
    }

    ColumnLayout {
        id: rows
        spacing: 10
        Layout.row: discord.wide ? 0 : 1
        Layout.column: 0
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop

        CheckBox {
            objectName: "setting_privacy.discordPresence"
            text: qsTr("Show what I am doing in Discord")
            leftPadding: 1
            spacing: 9
            Layout.fillWidth: true
            Accessible.name: text
            checked: discord.on
            onToggled: discord.put("privacy.discordPresence", checked)
        }
        Note { text: qsTr("Your Discord status shows what you choose below. Never folders, your subtitles, the video or line counts.") }

        SettingsSection {
            title: qsTr("What shows")
            Check { setting: "privacy.discordMode"; text: qsTr("The mode") }
            ColumnLayout {
                objectName: "discordModes"
                visible: discord.values["privacy.discordMode"] === true
                spacing: 6
                Layout.leftMargin: 24
                Layout.fillWidth: true
                Note { text: qsTr("The mode follows your layout. While one of the activities below is going, it shows instead.") }
                Repeater {
                    model: discord.app ? discord.app.presenceModes() : []
                    RowLayout {
                        id: mode
                        required property var modelData
                        spacing: 8
                        Layout.fillWidth: true
                        CheckBox {
                            objectName: "discordActivity_" + mode.modelData.key
                            enabled: discord.on && !mode.modelData.layout
                            checked: mode.modelData.layout || (discord.values["privacy.discordActivities"] ?? []).includes(mode.modelData.key)
                            leftPadding: 1
                            Accessible.name: mode.modelData.name
                            onToggled: discord.toggleActivity(mode.modelData.key, checked)
                        }
                        Rectangle {
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: discord.modeColours[mode.modelData.key]
                            Icon {
                                anchors.centerIn: parent
                                iconRole: discord.modeIcons[mode.modelData.key]
                                size: 12
                                color: "#1E2A6B"
                            }
                        }
                        Label { text: mode.modelData.name; color: Theme.text }
                        Label {
                            text: mode.modelData.when
                            color: Theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
                Note { text: qsTr("Unticked activities show the layout’s mode. A layout you saved shows the mode of the layout it came from.") }
            }
            Check { setting: "privacy.discordModeIcon"; text: qsTr("The mode’s icon") }
            Check { setting: "privacy.discordProjectName"; text: qsTr("The project’s name") }
            Check { setting: "privacy.discordFileName"; text: qsTr("The file name") }
        }

        SettingsSection {
            title: qsTr("Text")
            Choice {
                setting: "privacy.discordText"
                Accessible.name: qsTr("Text")
                options: [{value: "standard", text: qsTr("Standard")}, {value: "own", text: qsTr("My own")}]
            }
            ColumnLayout {
                visible: discord.values["privacy.discordText"] === "own"
                spacing: 8
                Layout.fillWidth: true
                Line { setting: "privacy.discordDetails"; name: qsTr("Details (first line)") }
                Line { setting: "privacy.discordState"; name: qsTr("State (second line)") }
                Flow {
                    objectName: "discordInsert"
                    spacing: 6
                    Layout.fillWidth: true
                    Label { text: qsTr("Insert"); color: Theme.muted }
                    Repeater {
                        model: discord.placeholders
                        Button {
                            required property string modelData
                            text: "{" + modelData + "}"
                            enabled: discord.on && discord.lastLine !== null
                            font.strikeout: discord.offSwitch(modelData) !== ""
                            focusPolicy: Qt.NoFocus
                            onClicked: {
                                discord.lastLine.insert(discord.lastLine.cursorPosition, text)
                                discord.lastLine.textEdited()
                            }
                        }
                    }
                }
            }
            RowLayout {
                spacing: 8
                Label { text: qsTr("Text from plugin"); color: Theme.text; Layout.preferredWidth: 160 }
                Choice {
                    setting: "privacy.discordPlugin"
                    Accessible.name: qsTr("Text from plugin")
                    options: [{value: "", text: qsTr("None")}].concat(
                        (discord.app ? discord.app.presencePlugins() : []).map(p => ({value: p.id, text: p.name})))
                }
            }
            Note { text: qsTr("Shows only where you put {plugin} in your own text.") }
        }

        SettingsSection {
            title: qsTr("Time")
            Choice {
                setting: "privacy.discordTime"
                Accessible.name: qsTr("Time shown")
                options: [{value: "started", text: qsTr("Since HikariSub started")},
                          {value: "file", text: qsTr("Since the file opened")},
                          {value: "hidden", text: qsTr("Hidden")}]
            }
        }

        SettingsSection {
            title: qsTr("Idle and away")
            RowLayout {
                spacing: 8
                Label { text: qsTr("After no input for"); color: Theme.text }
                SpinBox {
                    objectName: "setting_privacy.discordIdleMinutes"
                    enabled: discord.on
                    from: 1
                    to: 120
                    editable: true
                    value: discord.values["privacy.discordIdleMinutes"] ?? 10
                    Accessible.name: qsTr("Minutes without input")
                    onValueModified: discord.put("privacy.discordIdleMinutes", value)
                }
                Label { text: qsTr("min"); color: Theme.muted }
            }
            Choice {
                setting: "privacy.discordIdle"
                Accessible.name: qsTr("When idle")
                options: [{value: "idle", text: qsTr("Show “Idle”")},
                          {value: "clear", text: qsTr("Clear the status")},
                          {value: "keep", text: qsTr("Keep it")}]
            }
            Check { setting: "privacy.discordIdleBackground"; text: qsTr("Do the same while HikariSub is in the background") }
        }

        SettingsSection {
            title: qsTr("Buttons")
            Check { setting: "privacy.discordGetButton"; text: qsTr("“Get HikariSub”") }
            Check { setting: "privacy.discordLink"; text: qsTr("A link of your own") }
            GridLayout {
                visible: discord.values["privacy.discordLink"] === true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6
                Layout.leftMargin: 24
                Label { text: qsTr("Label"); color: Theme.text }
                TextField {
                    objectName: "setting_privacy.discordLinkLabel"
                    enabled: discord.on
                    maximumLength: 32
                    text: discord.values["privacy.discordLinkLabel"] ?? ""
                    Layout.preferredWidth: 260
                    Accessible.name: qsTr("Label")
                    onTextEdited: discord.put("privacy.discordLinkLabel", text)
                }
                Label { text: qsTr("Address"); color: Theme.text }
                TextField {
                    id: address
                    objectName: "setting_privacy.discordLinkUrl"
                    readonly property bool bad: text.length > 0 && !discord.validLink(text)
                    enabled: discord.on
                    text: discord.values["privacy.discordLinkUrl"] ?? ""
                    placeholderText: "https://"
                    Layout.preferredWidth: 260
                    Accessible.name: qsTr("Address")
                    onTextEdited: discord.put("privacy.discordLinkUrl", text)
                    Rectangle {
                        anchors.fill: parent
                        visible: address.bad
                        color: "transparent"
                        border.color: Theme.danger
                        radius: 4
                    }
                }
                Item { implicitWidth: 1 }
                Label {
                    objectName: "discordLinkProblem"
                    visible: address.bad
                    text: qsTr("Needs an https:// address.")
                    color: Theme.danger
                }
            }
        }
    }

    ColumnLayout {
        objectName: "discordPreview"
        spacing: 8
        Layout.row: 0
        Layout.column: discord.wide ? 1 : 0
        Layout.preferredWidth: 300
        Layout.maximumWidth: 300
        Layout.alignment: Qt.AlignTop

        Rectangle {
            id: cardBox
            readonly property var assets: discord.preview.assets ?? {}
            implicitWidth: 300
            implicitHeight: card.implicitHeight + 24
            radius: 8
            color: Theme.raised
            border.color: Theme.line
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Preview")
            ColumnLayout {
                id: card
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 8
                Label {
                    objectName: "discordPreviewOff"
                    visible: !discord.on
                    text: qsTr("Nothing is shown in Discord. Presence is off.")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: discord.on
                    spacing: 12
                    Item {
                        implicitWidth: 64
                        implicitHeight: 64
                        Image {
                            anchors.fill: parent
                            source: "qrc:/hikari/ui/app-icon.svg"
                            sourceSize: Qt.size(64, 64)
                        }
                        Rectangle {
                            readonly property string key: cardBox.assets.small_image ?? ""
                            visible: key !== ""
                            x: 44
                            y: 44
                            width: 24
                            height: 24
                            radius: 12
                            color: discord.modeColours[key] ?? Theme.raised
                            border.color: Theme.raised
                            border.width: 2
                            Icon {
                                anchors.centerIn: parent
                                iconRole: discord.modeIcons[parent.key] ?? ""
                                size: 14
                                color: "#1E2A6B"
                            }
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: "HikariSub"; font.bold: true; color: Theme.text }
                        Label {
                            objectName: "discordPreviewDetails"
                            text: discord.preview.details ?? ""
                            visible: text !== ""
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "discordPreviewState"
                            text: discord.preview.state ?? ""
                            visible: text !== ""
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: discord.preview.timestamps !== undefined
                            text: qsTr("12:34 elapsed")
                            color: Theme.muted
                        }
                    }
                }
                Repeater {
                    model: discord.on ? (discord.preview.buttons ?? []) : []
                    Rectangle {
                        required property var modelData
                        objectName: "discordPreviewButton"
                        implicitHeight: 32
                        radius: 4
                        color: Theme.background
                        border.color: Theme.line
                        Layout.fillWidth: true
                        Label {
                            anchors.centerIn: parent
                            text: parent.modelData.label
                            color: Theme.text
                        }
                    }
                }
            }
        }
        Note { text: qsTr("Roughly how Discord shows it to people who can see your status. Buttons show to them, not to you.") }
    }
}
