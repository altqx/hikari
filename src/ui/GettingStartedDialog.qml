pragma Translator: "HikariSub.GettingStarted"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: setup
    objectName: "gettingStartedDialog"
    required property var app
    required property var importer
    property var updates: null
    property var layouts: []
    title: qsTr("Getting started")
    dialogId: "gettingStarted"
    settings: app ? app.settings : null
    modal: true
    defaultButton: nextButton

    property var values: ({})
    property var languages: []
    property var dictionaries: []
    property var renderers: []
    property var rendererLabels: []
    property var outputDevices: []
    property bool legacy: false
    property bool importing: true
    property bool due: false
    property string importProblem
    property string layout: ""
    property int page: 0
    property var opened: ({})
    property var touched: ({})
    readonly property var pages: legacy ? ["language", "import", "look", "layout", "media", "editing", "updates", "files"]
                                        : ["language", "look", "layout", "media", "editing", "updates", "files"]
    readonly property string pageId: pages[page]
    readonly property bool lastPage: page === pages.length - 1
    signal reloaded()
    signal finished(string layout)
    signal skipped()

    function openSetup() {
        due = app.gettingStartedDue()
        const r = app.openGettingStarted()
        opened = r.values
        values = r.values
        touched = ({})
        languages = r.languages
        dictionaries = r.dictionaries
        renderers = r.renderers
        rendererLabels = r.rendererLabels
        outputDevices = r.outputDevices
        legacy = r.legacy === true
        importing = legacy
        importProblem = ""
        layout = ""
        page = 0
        if (legacy) {
            importer.discover()
            if (importer.roots.length > 0)
                importer.choose(importer.roots[0].path)
            review.reset()
        }
        reloaded()
        open()
    }
    function put(setting, value) {
        const v = Object.assign({}, values)
        v[setting] = value
        values = v
        const t = Object.assign({}, touched)
        t[setting] = true
        touched = t
        preview(setting)
    }
    function preview(setting) {
        if (setting === undefined || setting.startsWith("appearance."))
            Theme.preview(values)
        if (setting === undefined || setting === "program.language")
            app.previewLanguage(values["program.language"] ?? 0)
    }
    function fillFromImport() {
        const from = importing ? app.gettingStartedImported(opened) : opened
        const v = Object.assign({}, values)
        for (const id in from)
            if (!touched[id])
                v[id] = from[id]
        values = v
        preview()
        reloaded()
    }
    function next() {
        if (pageId === "import")
            fillFromImport()
        if (!lastPage) {
            page++
            return
        }
        if (!app.finishGettingStarted(values, legacy && importing)) {
            importProblem = importer.status
            page = pages.indexOf("import")
            return
        }
        accept()
    }

    onAccepted: {
        Theme.endPreview()
        finished(layout)
    }
    onRejected: {
        app.skipGettingStarted()
        Theme.endPreview()
        skipped()
    }

    readonly property string stagedTheme: Theme.code
    readonly property var stagedThemeInfo: Theme.themes().find(t => t.code === stagedTheme) ?? ({dark: Theme.dark, highContrast: Theme.highContrast})
    // Never read: it lists theme.cpp's names for lupdate; qsTranslate finds them by source.
    readonly property var themeTexts: [QT_TRANSLATE_NOOP("HikariSub.Options", "Light"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Dark"), QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast white"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast black"), QT_TRANSLATE_NOOP("HikariSub.Options", "Red"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Orange"), QT_TRANSLATE_NOOP("HikariSub.Options", "Gold"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Green"), QT_TRANSLATE_NOOP("HikariSub.Options", "Blue"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Purple"), QT_TRANSLATE_NOOP("HikariSub.Options", "Pink")]

    component SetupPage: Item {
        id: setupPage
        required property string title
        required property string explanation
        required property string later
        default property alias content: pageColumn.data
        ColumnLayout {
            id: pageColumn
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            spacing: 16
        }
    }
    component OptionCard: AbstractButton {
        id: card
        property string iconRole
        property string description
        property string badge
        checkable: false
        implicitWidth: 260
        implicitHeight: cardColumn.implicitHeight + 32
        Layout.fillHeight: true
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.description: description
        Accessible.checked: checked
        background: Rectangle {
            color: card.checked ? Theme.select : Theme.field
            border.color: card.checked ? Theme.accent : Theme.line
            border.width: card.checked ? 2 : 1
            FocusRing { control: card; radius: 2 }
        }
        contentItem: ColumnLayout {
            id: cardColumn
            spacing: 8
            Icon { iconRole: card.iconRole; size: 32; Layout.alignment: Qt.AlignHCenter }
            RowLayout {
                spacing: 8
                Layout.alignment: Qt.AlignHCenter
                RadioIndicator { on: card.checked }
                Label { text: card.text; font.bold: true }
            }
            Label {
                visible: card.badge.length > 0
                text: card.badge
                color: Theme.accent
                font: setup.fontAt(11, Font.DemiBold)
                leftPadding: 6
                rightPadding: 6
                Layout.alignment: Qt.AlignHCenter
                background: Rectangle { color: "transparent"; border.color: Theme.accent }
            }
            Label {
                text: card.description
                color: card.checked ? Theme.text : Theme.muted
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font: setup.fontAt(12)
                Layout.fillWidth: true
            }
        }
    }
    component RadioIndicator: Rectangle {
        property bool on: false
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: Theme.field
        border.color: on ? Theme.accent : Theme.control
        Rectangle { visible: parent.on; anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent }
    }
    component ThemeSample: AbstractButton {
        id: sample
        required property var theme
        readonly property var roles: Theme.rolesOf(theme.code, setup.values)
        implicitWidth: 220
        implicitHeight: 136 + 30
        Accessible.role: Accessible.RadioButton
        Accessible.name: qsTranslate("HikariSub.Options", theme.source)
        Accessible.checked: checked
        contentItem: ColumnLayout {
            spacing: 8
            Rectangle {
                implicitWidth: 220
                implicitHeight: 136
                color: sample.roles.background
                border.color: sample.checked ? Theme.accent : sample.roles.line
                border.width: sample.checked ? 2 : 1
                FocusRing { control: sample; radius: 2 }
                Rectangle { x: 2; y: 2; width: parent.width - 4; height: 12; color: sample.roles.panel
                    Row { x: 4; y: 4; spacing: 4; Repeater { model: 5; Rectangle { width: 14; height: 4; color: sample.roles.muted } } } }
                Rectangle { x: 3; y: 17; width: 92; height: 66; color: "#0B0E12"
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; width: 40; height: 3; color: "white" } }
                Rectangle { x: 98; y: 17; width: 119; height: 26; color: sample.roles.field; border.color: sample.roles.line
                    Rectangle { anchors.centerIn: parent; width: parent.width - 8; height: 2; color: sample.roles.accent } }
                Rectangle { x: 98; y: 46; width: 119; height: 37; color: sample.roles.panel; border.color: sample.roles.line
                    Rectangle { x: 4; y: 4; width: parent.width - 8; height: 12; color: sample.roles.field; border.color: sample.roles.accent } }
                Rectangle { x: 3; y: 86; width: parent.width - 6; height: 47; color: sample.roles.panel; border.color: sample.roles.line
                    Rectangle { x: 1; y: 3; width: parent.width - 2; height: 6; color: sample.roles.select }
                    Column { x: 4; y: 13; spacing: 4; Repeater { model: 4; Rectangle { width: 80 + (index % 2) * 40; height: 4; color: sample.roles.muted; required property int index } } } }
            }
            RowLayout {
                spacing: 8
                Layout.alignment: Qt.AlignHCenter
                RadioIndicator { on: sample.checked }
                Label { text: qsTranslate("HikariSub.Options", sample.theme.source); font.bold: true }
            }
        }
    }
    component LayoutCard: AbstractButton {
        id: layoutCard
        required property string preset
        property string description
        implicitWidth: 150
        implicitHeight: layoutColumn.implicitHeight + 16
        padding: 8
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.description: description
        Accessible.checked: checked
        background: Rectangle {
            color: layoutCard.checked ? Theme.select : Theme.field
            border.color: layoutCard.checked ? Theme.accent : Theme.line
            border.width: layoutCard.checked ? 2 : 1
            FocusRing { control: layoutCard; radius: 2 }
        }
        contentItem: ColumnLayout {
            id: layoutColumn
            spacing: 6
            LayoutThumb { preset: layoutCard.preset; Layout.fillWidth: true; Layout.preferredHeight: 96 }
            RowLayout {
                spacing: 8
                RadioIndicator { on: layoutCard.checked }
                Label { text: layoutCard.text; font.bold: true }
            }
            Label {
                text: layoutCard.description
                color: layoutCard.checked ? Theme.text : Theme.muted
                wrapMode: Text.WordWrap
                font: setup.fontAt(12)
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(implicitHeight, 48)
            }
        }
    }
    component Block: Rectangle {
        property string label
        property bool video: false
        property bool audio: false
        color: video ? "#0B0E12" : audio ? Theme.field : Theme.panel
        border.color: Theme.line
        Layout.fillWidth: true
        Layout.fillHeight: true
        Label {
            anchors.centerIn: parent
            width: parent.width - 4
            text: parent.label
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: parent.video ? "#C8CED4" : parent.audio ? Theme.accent : Theme.muted
            font: setup.fontAt(10)
        }
    }
    component LayoutThumb: Rectangle {
        id: thumb
        required property string preset
        color: Theme.background
        border.color: Theme.line
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2
            RowLayout {
                visible: thumb.preset === "Translation"
                spacing: 2
                Layout.preferredHeight: 58
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 44 }
                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 56
                    Block { label: qsTr("Original text") }
                    Block { label: qsTr("Line editor") }
                }
            }
            Block { visible: thumb.preset === "Timing"; label: qsTr("Audio"); audio: true; Layout.preferredHeight: 30 }
            Block { visible: thumb.preset === "Timing"; label: qsTr("Line editor"); Layout.preferredHeight: 26 }
            RowLayout {
                visible: thumb.preset === "Typesetting"
                spacing: 2
                Layout.preferredHeight: 64
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 74 }
                Block { label: qsTr("Styles"); Layout.preferredWidth: 26 }
            }
            RowLayout {
                visible: thumb.preset === "Typesetting"
                spacing: 2
                Layout.preferredHeight: 36
                Block { label: qsTr("Line editor"); Layout.preferredWidth: 55 }
                Block { label: qsTr("Grid"); Layout.preferredWidth: 45 }
            }
            RowLayout {
                visible: thumb.preset === "Karaoke"
                spacing: 2
                Layout.preferredHeight: 44
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 44 }
                Block { label: qsTr("Template editor"); Layout.preferredWidth: 56 }
            }
            Block { visible: thumb.preset === "Karaoke"; label: qsTr("Audio"); audio: true; Layout.preferredHeight: 30 }
            Block { visible: thumb.preset !== "Typesetting"; label: qsTr("Grid"); Layout.preferredHeight: thumb.preset === "Karaoke" ? 26 : 42 }
        }
    }
    component PageNumber: RowLayout {
        id: numberRow
        required property string setting
        property string unit
        property string name
        property int from: 0
        property int to: 100000
        spacing: 6
        SpinBox {
            id: spin
            objectName: "gettingStarted_" + numberRow.setting
            from: numberRow.from
            to: numberRow.to
            editable: true
            Accessible.name: numberRow.name
            Layout.preferredWidth: 110
            function refresh() { value = setup.values[numberRow.setting] ?? 0 }
            Component.onCompleted: refresh()
            Connections { target: setup; function onReloaded() { spin.refresh() } }
            onValueModified: setup.put(numberRow.setting, value)
        }
        Label { visible: numberRow.unit.length > 0; text: numberRow.unit; color: Theme.muted }
    }
    component PageChoice: ComboBox {
        id: choice
        required property string setting
        objectName: "gettingStarted_" + setting
        Layout.preferredWidth: 300
        function refresh() {
            const v = setup.values[setting] ?? -1
            currentIndex = v >= 0 && v < count ? v : -1
        }
        Component.onCompleted: refresh()
        onModelChanged: refresh()
        onCountChanged: refresh()
        Connections { target: setup; function onReloaded() { choice.refresh() } }
        onActivated: index => setup.put(setting, index)
    }

    ColumnLayout {
        spacing: 0
        Layout.preferredWidth: 704
        Layout.preferredHeight: 520

        Row {
            objectName: "gettingStartedDots"
            spacing: 8
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            Layout.bottomMargin: 18
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Step %1 of %2").arg(setup.page + 1).arg(setup.pages.length)
            Repeater {
                model: setup.pages.length
                Rectangle {
                    required property int index
                    width: 8
                    height: 8
                    radius: 4
                    color: index === setup.page ? Theme.accent : Theme.disabled
                    opacity: index === setup.page ? 1 : 0.6
                }
            }
        }

        Label {
            objectName: "gettingStartedTitle"
            text: pagesStack.shown ? pagesStack.shown.title : ""
            font: setup.fontAt(16, Font.DemiBold)
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
        }
        Label {
            text: pagesStack.shown ? pagesStack.shown.explanation : ""
            color: Theme.muted
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: 22
        }
        StackLayout {
            id: pagesStack
            objectName: "gettingStartedPages"
            readonly property var shown: currentIndex >= 0 && currentIndex < children.length ? children[currentIndex] : null
            currentIndex: ["language", "import", "look", "layout", "media", "editing", "updates", "files"].indexOf(setup.pageId)
            Layout.fillWidth: true
            Layout.fillHeight: true

            SetupPage {
                objectName: "gettingStartedLanguage"
                title: qsTr("Welcome to HikariSub")
                explanation: qsTr("Let’s get started by choosing your language. Nothing is saved until Finish, and every choice can be changed later.")
                later: qsTr("Later: Settings › General › Language")
                ListView {
                    id: languageList
                    objectName: "gettingStartedLanguages"
                    model: setup.languages
                    currentIndex: setup.values["program.language"] ?? 0
                    implicitHeight: Math.min(count, 8) * 30 + 4
                    clip: true
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    highlightMoveDuration: 0
                    Layout.preferredWidth: 340
                    Layout.preferredHeight: implicitHeight
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Interface language")
                    onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex !== (setup.values["program.language"] ?? 0))
                                               setup.put("program.language", currentIndex)
                    Connections {
                        target: setup
                        function onReloaded() { languageList.currentIndex = setup.values["program.language"] ?? 0 }
                    }
                    delegate: ItemDelegate {
                        id: languageRow
                        required property string modelData
                        required property int index
                        objectName: "gettingStartedLanguage_" + index
                        width: ListView.view.width
                        height: 30
                        highlighted: ListView.isCurrentItem
                        focusPolicy: Qt.NoFocus
                        onClicked: languageList.currentIndex = index
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData
                        Accessible.checked: highlighted
                        background: Rectangle {
                            color: languageRow.highlighted ? Theme.select : "transparent"
                            Rectangle { visible: languageRow.highlighted; width: 3; height: parent.height; color: Theme.accent }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            RadioIndicator { on: languageRow.highlighted }
                            Label { text: languageRow.modelData; Layout.fillWidth: true }
                        }
                    }
                    Rectangle { z: -1; anchors.fill: parent; color: Theme.field; border.color: Theme.line }
                }
                Label {
                    text: qsTr("This window switches to the language you pick.")
                    color: Theme.muted
                    font: setup.fontAt(12)
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            SetupPage {
                id: importPage
                objectName: "gettingStartedImport"
                title: setup.importing ? qsTr("Choose what to bring over") : qsTr("Bring over your old settings?")
                explanation: setup.importing
                             ? qsTr("The next pages start from these values; a change there wins over the imported one.")
                             : qsTr("HikariSub 0.0.x is installed on this computer. Nothing in it is changed.")
                later: qsTr("Later: File › Import legacy settings…")
                RowLayout {
                    id: importChoices
                    spacing: 16
                    Layout.alignment: Qt.AlignHCenter
                    OptionCard {
                        objectName: "gettingStartedImportYes"
                        iconRole: "import"
                        text: qsTr("Import my old settings")
                        badge: qsTr("Recommended")
                        description: qsTr("Hotkeys, fonts, audio and video options and your dictionary words.")
                        checked: setup.importing
                        onClicked: setup.importing = true
                        Layout.preferredWidth: 300
                    }
                    OptionCard {
                        objectName: "gettingStartedImportNo"
                        iconRole: "settings"
                        text: qsTr("Set up from scratch")
                        description: qsTr("Start from HikariSub’s defaults. File › Import legacy settings… can still bring them over later.")
                        checked: !setup.importing
                        onClicked: setup.importing = false
                        Layout.preferredWidth: 300
                    }
                }
                RowLayout {
                    id: importProblemRow
                    visible: setup.importing && setup.importProblem.length > 0
                    spacing: 8
                    Layout.fillWidth: true
                    Icon {
                        iconRole: "notice-error"
                        size: 14
                        color: Theme.danger
                        accentColor: Theme.danger
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                    }
                    Label {
                        objectName: "gettingStartedImportProblem"
                        text: setup.importProblem
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.AlertMessage
                    }
                    LinkButton {
                        objectName: "gettingStartedReadAgain"
                        iconRole: "refresh"
                        text: qsTr("Read again")
                        onClicked: {
                            if (setup.importer.choose(setup.importer.root))
                                setup.importProblem = ""
                            else
                                setup.importProblem = setup.importer.status
                            review.reset()
                        }
                    }
                }
                RowLayout {
                    id: encodingRow
                    visible: setup.importing && (setup.importer.ambiguousEncoding || setup.importer.interpretation > 0)
                    spacing: 8
                    Label { text: qsTranslate("HikariSub.SettingsImport", "Files that are not UTF-8 are read as") }
                    ComboBox {
                        objectName: "gettingStartedInterpretation"
                        Layout.preferredWidth: 200
                        model: [qsTranslate("HikariSub.SettingsImport", "Choose…"),
                                qsTranslate("HikariSub.SettingsImport", "Latin-1 (ISO 8859-1)"),
                                qsTranslate("HikariSub.SettingsImport", "Windows-1252")]
                        currentIndex: setup.importer.interpretation
                        onActivated: index => {
                            setup.importer.interpretation = index
                            review.reset()
                        }
                    }
                }
                SettingsImportReview {
                    id: review
                    objectName: "gettingStartedReview"
                    visible: setup.importing
                    importer: setup.importer
                    window: setup
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(140, importPage.height - importChoices.height - 16
                                                     - (importProblemRow.visible ? importProblemRow.height + 16 : 0)
                                                     - (encodingRow.visible ? encodingRow.height + 16 : 0))
                }
            }

            SetupPage {
                objectName: "gettingStartedLook"
                title: qsTr("Look")
                explanation: qsTr("Choose a theme. This window changes as you choose.")
                later: qsTr("Later: Settings › Appearance")
                CheckBox {
                    objectName: "gettingStartedFollowSystem"
                    text: qsTr("Follow system theme")
                    checked: setup.values["appearance.followSystem"] === true
                    onToggled: setup.put("appearance.followSystem", checked)
                    Layout.alignment: Qt.AlignHCenter
                }
                RowLayout {
                    spacing: 48
                    Layout.alignment: Qt.AlignHCenter
                    Repeater {
                        model: Theme.themes().filter(t => t.highContrast === (setup.stagedThemeInfo.highContrast === true))
                        ThemeSample {
                            required property var modelData
                            objectName: "gettingStartedTheme_" + modelData.code
                            theme: modelData
                            checked: setup.stagedTheme === modelData.code
                            onClicked: {
                                setup.put("appearance.theme", modelData.code)
                                if (setup.values["appearance.followSystem"] === true)
                                    setup.put("appearance.followSystem", false)
                            }
                        }
                    }
                }
                RowLayout {
                    objectName: "gettingStartedAccents"
                    visible: setup.stagedThemeInfo.highContrast !== true
                    spacing: 4
                    Layout.alignment: Qt.AlignHCenter
                    readonly property string setting: setup.stagedThemeInfo.dark ? "appearance.darkAccent" : "appearance.lightAccent"
                    readonly property var accents: Theme.accents(setup.stagedThemeInfo.dark === true)
                    readonly property string chosen: setup.values[setting] ?? "green"
                    Label { text: qsTr("Accent colour"); Layout.rightMargin: 10 }
                    Repeater {
                        model: parent.accents
                        AbstractButton {
                            id: swatch
                            required property var modelData
                            objectName: "gettingStartedAccent_" + modelData.key
                            readonly property bool chosen: parent.chosen === modelData.key
                            implicitWidth: 24
                            implicitHeight: 24
                            ToolTip.text: qsTranslate("HikariSub.Options", modelData.source)
                            ToolTip.visible: hovered
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: ToolTip.text
                            Accessible.checked: chosen
                            onClicked: setup.put(parent.setting, modelData.key)
                            background: Rectangle {
                                radius: width / 2
                                color: "transparent"
                                border.color: swatch.chosen ? Theme.text : "transparent"
                                border.width: 2
                                Rectangle { anchors.centerIn: parent; width: 18; height: 18; radius: 9; color: swatch.modelData.accent }
                                FocusRing { control: swatch; radius: 12 }
                            }
                        }
                    }
                    Label {
                        text: qsTranslate("HikariSub.Options", (parent.accents.find(a => a.key === parent.chosen) ?? {source: ""}).source)
                        color: Theme.muted
                        font: setup.fontAt(12)
                        Layout.leftMargin: 10
                    }
                }
                Label {
                    visible: setup.stagedThemeInfo.highContrast === true
                    text: qsTr("Further high contrast settings are in Settings › Appearance.")
                    color: Theme.muted
                    font: setup.fontAt(12)
                    Layout.alignment: Qt.AlignHCenter
                }
                CheckBox {
                    objectName: "gettingStartedHighContrast"
                    text: qsTr("Enable high contrast")
                    checked: setup.stagedThemeInfo.highContrast === true
                    onToggled: setup.put("appearance.theme", checked
                                          ? (setup.stagedThemeInfo.dark ? "highContrastBlack" : "highContrastWhite")
                                          : (setup.stagedThemeInfo.dark ? "dark" : "light"))
                    Layout.alignment: Qt.AlignHCenter
                }
                RowLayout {
                    visible: WindowFrame.customAvailable
                    spacing: 10
                    Layout.alignment: Qt.AlignHCenter
                    Label { text: qsTr("Title bar") }
                    SegmentedButtons {
                        objectName: "gettingStartedTitleBar"
                        choices: [{ text: qsTr("HikariSub’s"), name: "gettingStartedTitleBarOwn" },
                                  { text: qsTr("The system’s"), name: "gettingStartedTitleBarSystem" }]
                        currentIndex: WindowFrame.usesSystemTitleBar(setup.values["appearance.titleBar"] ?? "auto") ? 1 : 0
                        // MUST KILL: picking the desktop's default stores "auto", not the choice
                        onActivated: index => setup.put("appearance.titleBar",
                                                         (index === 1) === WindowFrame.systemTitleBarByDefault ? "auto"
                                                             : index === 1 ? "system" : "custom")
                    }
                    Label {
                        text: WindowFrame.systemTitleBarByDefault ? qsTr("The system’s suits this desktop.")
                                                                  : qsTr("HikariSub’s suits this desktop.")
                        color: Theme.muted
                        font: setup.fontAt(12)
                    }
                }
            }

            SetupPage {
                objectName: "gettingStartedLayout"
                title: qsTr("What do you mostly do?")
                explanation: qsTr("Pick one to set the layout your first tab opens with. Leave it to keep Editing, which has a bit of everything.")
                later: qsTr("Later: View › Layout, or the layout switcher in the title bar")
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Repeater {
                        model: setup.layouts.filter(p => p !== "Editing")
                        LayoutCard {
                            required property string modelData
                            objectName: "gettingStartedLayout_" + modelData
                            preset: modelData
                            text: ({Translation: qsTr("Translate"), Timing: qsTr("Time"),
                                    Typesetting: qsTr("Typeset"), Karaoke: qsTr("Karaoke")})[modelData] ?? modelData
                            description: ({Translation: qsTr("The original text above your translation, with Translation mode on."),
                                           Timing: qsTr("Audio across the top for timing lines by ear."),
                                           Typesetting: qsTr("A large video with the visual tools and the Styles panel."),
                                           Karaoke: qsTr("The Template editor beside the video, the audio in Karaoke mode.")})[modelData] ?? ""
                            checked: setup.layout === modelData
                            onClicked: setup.layout = setup.layout === modelData ? "" : modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                        }
                    }
                }
            }

            SetupPage {
                objectName: "gettingStartedMedia"
                title: qsTr("Video and audio")
                explanation: qsTr("How subtitles are drawn on the video, and where the sound goes.")
                later: qsTr("Later: Settings › Video and Settings › Audio")
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 620
                    Label { text: qsTr("Subtitles drawn by") }
                    RowLayout {
                        spacing: 10
                        PageChoice {
                            setting: "video.subtitleProvider"
                            visible: setup.renderers.length > 1
                            model: setup.rendererLabels
                            Accessible.name: qsTr("Subtitles drawn by")
                        }
                        Label {
                            visible: setup.renderers.length <= 1
                            text: "libass"
                        }
                        Label {
                            visible: setup.renderers.length <= 1
                            text: qsTr("The only renderer here.")
                            color: Theme.muted
                            font: setup.fontAt(12)
                        }
                    }
                    Item { implicitWidth: 1 }
                    Image {
                        objectName: "gettingStartedFrame"
                        source: setup.visible && setup.pageId === "media"
                                ? setup.app.sampleFrame(setup.values["video.subtitleProvider"] ?? 0, 480, 135) : ""
                        sourceSize: Qt.size(480, 135)
                        cache: false
                        Layout.preferredWidth: 480
                        Layout.preferredHeight: 135
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("A sample frame with a subtitle")
                    }
                    Label { text: qsTr("Sound plays on") }
                    RowLayout {
                        spacing: 10
                        PageChoice {
                            setting: "audio.outputDevice"
                            model: setup.outputDevices
                            Accessible.name: qsTr("Sound plays on")
                        }
                        Button {
                            objectName: "gettingStartedTestSound"
                            text: qsTr("Test sound")
                            onClicked: setup.app.playTestSound(setup.values["audio.outputDevice"] ?? 0)
                        }
                    }
                }
            }

            SetupPage {
                objectName: "gettingStartedEditing"
                title: qsTr("Editing")
                explanation: qsTr("Backups while you work, and the checks on the line editor’s text.")
                later: qsTr("Later: Settings › General › Saving and Settings › Spelling & grammar")
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 620
                    Label { text: qsTr("Autosave files kept") }
                    PageNumber { setting: "autosave.maxFiles"; from: 2; to: 1000000; name: qsTr("Autosave files kept") }
                    Label { text: qsTr("Spelling") }
                    RowLayout {
                        spacing: 10
                        CheckBox {
                            objectName: "gettingStartedSpelling"
                            text: qsTr("Check spelling")
                            checked: setup.values["editor.spellchecker"] === true
                            onToggled: setup.put("editor.spellchecker", checked)
                        }
                        PageChoice {
                            setting: "editor.dictionaryLanguage"
                            model: setup.dictionaries
                            enabled: setup.values["editor.spellchecker"] === true
                            Layout.preferredWidth: 240
                            Accessible.name: qsTr("Primary language")
                        }
                    }
                    Label { text: qsTr("Grammar") }
                    PageChoice {
                        setting: "grammar.language"
                        model: [qsTr("Off"), qsTr("English (US)"), qsTr("English (UK)"), qsTr("English (Canada)"),
                                qsTr("English (Australia)")]
                        Layout.preferredWidth: 240
                        Accessible.name: qsTr("Grammar check")
                    }
                    Label { text: qsTr("Example"); color: Theme.muted; font: setup.fontAt(12) }
                    TextArea {
                        id: sampleLine
                        objectName: "gettingStartedSample"
                        readOnly: true
                        text: qsTr("She has seen teh last episode already.")
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Example")
                        EditorHighlighter {
                            document: sampleLine.textDocument
                            colour: Theme.spellcheck
                            ranges: setup.visible && setup.pageId === "editing"
                                    ? setup.app.sampleSpellingMarks(sampleLine.text, setup.values["editor.spellchecker"] === true,
                                                                    setup.values["editor.dictionaryLanguage"] ?? -1)
                                    : []
                        }
                    }
                }
            }

            SetupPage {
                objectName: "gettingStartedUpdates"
                title: qsTr("Updates and privacy")
                explanation: qsTr("Which versions HikariSub offers you, whether it looks for them by itself, and whether it sends anonymous usage data.")
                later: qsTr("Later: Settings › Updates and privacy")
                RowLayout {
                    visible: !setup.updates || setup.updates.stableOnlyOption
                    spacing: 16
                    Layout.alignment: Qt.AlignHCenter
                    OptionCard {
                        objectName: "gettingStartedStable"
                        iconRole: "check-updates"
                        text: qsTr("Stable versions only")
                        badge: qsTr("Recommended")
                        description: qsTr("Tested releases.")
                        checked: setup.values["updater.checkForStable"] === true
                        onClicked: setup.put("updater.checkForStable", true)
                    }
                    OptionCard {
                        objectName: "gettingStartedPrereleases"
                        iconRole: "whats-new"
                        text: qsTr("Pre-releases too")
                        description: qsTr("Beta versions as well, to try what’s coming. They can have bugs.")
                        checked: setup.values["updater.checkForStable"] === false
                        onClicked: setup.put("updater.checkForStable", false)
                    }
                }
                CheckBox {
                    objectName: "gettingStartedAutoCheck"
                    text: qsTr("Check for updates automatically")
                    checked: setup.values["updater.autoCheck"] === true
                    onToggled: setup.put("updater.autoCheck", checked)
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("At most once a day, when HikariSub starts. What’s new on Home needs it too.")
                    color: Theme.muted
                    font: setup.fontAt(12)
                    Layout.alignment: Qt.AlignHCenter
                }
                CheckBox {
                    objectName: "gettingStartedUsageData"
                    text: qsTr("Send anonymous usage data")
                    checked: setup.values["privacy.usageData"] === true
                    onToggled: setup.put("privacy.usageData", checked)
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                }
                Label {
                    text: qsTr("Which features are used, errors and crashes, and how fast things open. Never your files, their names or what is in them.")
                    color: Theme.muted
                    font: setup.fontAt(12)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: 460
                    Layout.alignment: Qt.AlignHCenter
                }
                LinkButton {
                    objectName: "gettingStartedWhatIsSent"
                    iconRole: "help"
                    text: qsTr("What is sent")
                    onClicked: setup.app.openWhatIsSent()
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            SetupPage {
                objectName: "gettingStartedFiles"
                title: qsTr("Your files")
                explanation: qsTr("Everything of yours is in one folder. Copy it to move your setup to another computer.")
                later: qsTr("Later: Settings › General")
                RowLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignHCenter
                    TextField {
                        objectName: "gettingStartedUserFolder"
                        readOnly: true
                        text: setup.app.userFolder
                        Layout.preferredWidth: 420
                        Accessible.name: qsTr("Your folder")
                    }
                    Button {
                        objectName: "gettingStartedOpenFolder"
                        text: qsTr("Open folder")
                        onClicked: setup.app.openUserFolder()
                    }
                }
            }
        }

        RowLayout {
            spacing: 6
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            Icon { iconRole: "settings"; size: 14; color: Theme.muted }
            Label {
                objectName: "gettingStartedLater"
                text: pagesStack.shown ? pagesStack.shown.later : ""
                color: Theme.muted
                font: setup.fontAt(12)
            }
        }
    }

    leadingActions: [
        Button {
            objectName: "gettingStartedBack"
            visible: setup.page > 0
            text: qsTr("Back")
            onClicked: setup.page--
        }
    ]
    buttons: [
        Button {
            objectName: "gettingStartedSkip"
            text: qsTr("Skip setup")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: setup.reject()
        },
        PrimaryButton {
            id: nextButton
            objectName: "gettingStartedNext"
            text: setup.lastPage ? qsTr("Finish") : qsTr("Next")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: setup.next()
        }
    ]
}
