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
    property string openAnythingKeys: "Ctrl+P"
    title: qsTr("Getting started")
    dialogId: "gettingStarted"
    settings: app ? app.settings : null
    modal: true
    flush: true
    resizable: true
    footerRule: true
    smallestWidth: 760
    smallestHeight: 560
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
    property string finishProblem
    property string layout: ""
    property string afterwards: ""
    property int page: 0
    property var visited: ({})
    property var opened: ({})
    property var touched: ({})
    readonly property var pages: legacy ? ["language", "import", "look", "layout", "media", "editing", "ai", "updates", "ready"]
                                        : ["language", "look", "layout", "media", "editing", "ai", "updates", "ready"]
    readonly property string pageId: pages[page]
    readonly property bool lastPage: page === pages.length - 1
    readonly property var stepNames: ({language: qsTr("Welcome"), import: qsTr("Bring over"), look: qsTr("Look"),
                                       layout: qsTr("How you work"), media: qsTr("Video and audio"), editing: qsTr("Editing"),
                                       ai: qsTr("AI"), updates: qsTr("Updates and privacy"), ready: qsTr("Ready")})
    readonly property var layoutNames: ({"": qsTr("Editing"), Translation: qsTr("Translate"), Timing: qsTr("Time"),
                                         Typesetting: qsTr("Typeset"), Karaoke: qsTr("Karaoke"), fullScreen: qsTr("Full screen")})
    readonly property bool turnsAiOn: values["ai.off"] === false && opened["ai.off"] !== false
    signal reloaded()
    signal finished(string layout, bool aiOn, string settingShown)
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
        finishProblem = ""
        layout = ""
        afterwards = ""
        page = 0
        visited = ({language: true})
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
    function goTo(index) {
        if (index < 0 || index >= pages.length || index === page)
            return
        if (pageId === "import")
            fillFromImport()
        page = index
        const v = Object.assign({}, visited)
        v[pageId] = true
        visited = v
    }
    function next() {
        if (lastPage)
            finish("")
        else
            goTo(page + 1)
    }
    function finish(settingShown) {
        if (pageId === "import")
            fillFromImport()
        finishProblem = ""
        if (!app.finishGettingStarted(values, legacy && importing)) {
            if (pages.includes("import")) {
                importProblem = importer.status
                page = pages.indexOf("import")
            } else {
                finishProblem = importer.status
            }
            return
        }
        afterwards = settingShown
        accept()
    }
    function chooseTheme(theme) {
        put("appearance.theme", theme.code)
        put(pairSettings[theme.type] ?? "appearance.theme", theme.code)
        if (values["appearance.followSystem"] === true)
            put("appearance.followSystem", false)
    }
    function foundNames() {
        const names = []
        for (const root of importer.roots)
            if (!names.includes(root.name))
                names.push(root.name)
        return names
    }
    function foundText() {
        const names = foundNames()
        if (names.length === 1)
            return qsTr("%1’s settings were found on this computer. Nothing in them is changed.").arg(names[0])
        if (names.length === 2)
            return qsTr("Settings from %1 and %2 were found on this computer. Nothing in them is changed.").arg(names[0], names[1])
        return qsTr("Settings from %1, %2 and %3 were found on this computer. Nothing in them is changed.")
            .arg(names[0] ?? "", names[1] ?? "", names[2] ?? "")
    }

    onAccepted: {
        Theme.endPreview()
        finished(layout, turnsAiOn, afterwards)
    }
    onRejected: {
        app.skipGettingStarted()
        Theme.endPreview()
        skipped()
    }

    Shortcut {
        sequences: ["Ctrl+PgDown"]
        onActivated: setup.goTo(setup.page + 1)
    }
    Shortcut {
        sequences: ["Ctrl+PgUp"]
        onActivated: setup.goTo(setup.page - 1)
    }
    Shortcut {
        sequences: ["Alt+Left"]
        onActivated: setup.goTo(setup.page - 1)
    }

    readonly property string stagedTheme: Theme.code
    readonly property var pairSettings: ({light: "appearance.lightTheme", dark: "appearance.darkTheme",
                                          highContrastLight: "appearance.highContrastLightTheme",
                                          highContrastDark: "appearance.highContrastDarkTheme"})
    readonly property var allThemes: Theme.themeList
    readonly property var stagedThemeInfo: allThemes.find(t => t.code === stagedTheme) ?? ({dark: Theme.dark, highContrast: Theme.highContrast})
    readonly property var iconChoices: [{value: "hikarisub", text: qsTr("HikariSub (built in)")}]
        .concat(IconTheme.themes.map(t => ({value: t.id, text: t.title + " — " + t.plugin})))
    // Never read: it lists theme.cpp's names for lupdate; qsTranslate finds them by source.
    readonly property var themeTexts: [QT_TRANSLATE_NOOP("HikariSub.Options", "Light"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Dark"), QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast white"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast black"), QT_TRANSLATE_NOOP("HikariSub.Options", "Red"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Orange"), QT_TRANSLATE_NOOP("HikariSub.Options", "Gold"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Green"), QT_TRANSLATE_NOOP("HikariSub.Options", "Blue"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Purple"), QT_TRANSLATE_NOOP("HikariSub.Options", "Pink")]

    component SetupPage: ScrollView {
        id: setupPage
        required property string title
        required property string explanation
        property string later
        property string laterSetting
        property bool fills: false
        default property alias content: pageColumn.data
        contentWidth: availableWidth
        contentHeight: fills ? availableHeight : pageColumn.implicitHeight
        ScrollBar.vertical.policy: fills ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        clip: true
        ColumnLayout {
            id: pageColumn
            width: setupPage.availableWidth
            spacing: 14
        }
    }
    component GroupLabel: Label {
        color: Theme.muted
        font: setup.fontAt(11.5, Font.DemiBold)
    }
    component Card: AbstractButton {
        id: card
        property bool chosen: false
        checkable: false
        checked: chosen
        activeFocusOnTab: true
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.checked: chosen
        background: Rectangle {
            radius: 6
            color: card.chosen ? Theme.select : card.hovered ? Theme.raised : Theme.field
            border.color: card.chosen ? Theme.accent : Theme.line
            border.width: card.chosen ? 2 : 1
            FocusRing { control: card; radius: 6 }
        }
    }
    component OptionCard: Card {
        id: option
        property string iconRole
        property string description
        property string badge
        implicitWidth: 260
        implicitHeight: optionColumn.implicitHeight + 28
        padding: 14
        Accessible.description: description
        contentItem: ColumnLayout {
            id: optionColumn
            spacing: 8
            RowLayout {
                spacing: 8
                Icon { iconRole: option.iconRole; size: 18 }
                Label { text: option.text; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Label {
                    visible: option.badge.length > 0
                    text: option.badge
                    color: Theme.accent
                    font: setup.fontAt(11, Font.DemiBold)
                    leftPadding: 6
                    rightPadding: 6
                    background: Rectangle { color: "transparent"; radius: Theme.radius; border.color: Theme.accent }
                }
            }
            Label {
                text: option.description
                color: Theme.muted
                wrapMode: Text.WordWrap
                font: setup.fontAt(12)
                Layout.fillWidth: true
            }
        }
    }
    component ThemeCard: Card {
        id: themeCard
        required property var theme
        readonly property var roles: Theme.rolesOf(theme.code, setup.values)
        text: theme.name
        implicitWidth: 140
        implicitHeight: themeColumn.implicitHeight + 12
        padding: 6
        contentItem: ColumnLayout {
            id: themeColumn
            spacing: 8
            Rectangle {
                radius: 6
                color: themeCard.roles.background
                border.color: themeCard.roles.line
                clip: true
                Layout.fillWidth: true
                Layout.preferredHeight: 74
                Rectangle { x: 1; y: 1; width: parent.width - 2; height: 12; radius: 5; color: themeCard.roles.panel }
                Rectangle { x: 6; y: 18; width: parent.width * 0.42; height: 50; radius: 2; color: "#0B0E12" }
                Rectangle { x: parent.width * 0.42 + 10; y: 18; width: parent.width * 0.58 - 16; height: 22; radius: 2
                    color: themeCard.roles.field; border.color: themeCard.roles.line
                    Rectangle { anchors.centerIn: parent; width: parent.width - 8; height: 2; color: themeCard.roles.accent } }
                Rectangle { x: parent.width * 0.42 + 10; y: 44; width: parent.width * 0.58 - 16; height: 24; radius: 2
                    color: themeCard.roles.panel; border.color: themeCard.roles.line
                    Rectangle { x: 3; y: 3; width: parent.width - 6; height: 6; color: themeCard.roles.select } }
            }
            Label { text: themeCard.text; elide: Text.ElideRight; Layout.fillWidth: true }
            Label {
                visible: themeCard.theme.plugin.length > 0
                text: themeCard.theme.plugin
                color: Theme.muted
                font: setup.fontAt(11)
                elide: Text.ElideRight
                Layout.fillWidth: true
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
            font: setup.fontAt(9)
        }
    }
    component LayoutThumb: Rectangle {
        id: thumb
        required property string preset
        color: Theme.background
        border.color: Theme.line
        radius: Theme.radius
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2
            RowLayout {
                visible: thumb.preset === ""
                spacing: 2
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 44 }
                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 56
                    Block { label: qsTr("Audio"); audio: true }
                    Block { label: qsTr("Line editor") }
                }
            }
            RowLayout {
                visible: thumb.preset === "Translation"
                spacing: 2
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 44 }
                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 56
                    Block { label: qsTr("Original text") }
                    Block { label: qsTr("Line editor") }
                }
            }
            Block { visible: thumb.preset === "Timing"; label: qsTr("Audio"); audio: true }
            Block { visible: thumb.preset === "Timing"; label: qsTr("Line editor") }
            RowLayout {
                visible: thumb.preset === "Typesetting"
                spacing: 2
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 74 }
                Block { label: qsTr("Style"); Layout.preferredWidth: 26 }
            }
            RowLayout {
                visible: thumb.preset === "Karaoke"
                spacing: 2
                Block { label: qsTr("Video"); video: true; Layout.preferredWidth: 44 }
                Block { label: qsTr("Template editor"); Layout.preferredWidth: 56 }
            }
            Block { visible: thumb.preset === "Karaoke"; label: qsTr("Audio"); audio: true }
            Block { visible: thumb.preset === "fullScreen"; label: qsTr("Video"); video: true; Layout.preferredHeight: 3 }
            Block { visible: thumb.preset === "fullScreen"; label: qsTr("Line editor"); Layout.preferredHeight: 1 }
            Block { visible: thumb.preset !== "fullScreen" && thumb.preset !== "Timing"; label: qsTr("Grid") }
        }
    }
    component LayoutCard: Card {
        id: layoutCard
        required property string preset
        property string description
        text: setup.layoutNames[preset]
        implicitWidth: 150
        implicitHeight: layoutColumn.implicitHeight + 16
        padding: 8
        Accessible.description: description
        objectName: "gettingStartedLayout_" + (preset.length > 0 ? preset : "Editing")
        chosen: setup.layout === preset
        onClicked: setup.layout = preset
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        contentItem: ColumnLayout {
            id: layoutColumn
            spacing: 8
            LayoutThumb { preset: layoutCard.preset; Layout.fillWidth: true; Layout.preferredHeight: 58 }
            Label { text: layoutCard.text; font.weight: Font.DemiBold }
            Label {
                text: layoutCard.description
                color: Theme.muted
                wrapMode: Text.WordWrap
                font: setup.fontAt(12)
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(implicitHeight, 44)
            }
        }
    }
    component PageNumber: RowLayout {
        id: numberRow
        required property string setting
        property string unit
        property string name
        property int from: 0
        property int to: 100000
        spacing: 8
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
    component SummaryRow: AbstractButton {
        id: row
        required property string step
        required property string iconRole
        required property string name
        property string value
        objectName: "gettingStartedSummary_" + step
        implicitHeight: 30
        leftPadding: 8
        rightPadding: 8
        Layout.fillWidth: true
        Accessible.role: Accessible.Button
        Accessible.name: name + ", " + value
        onClicked: setup.goTo(setup.pages.indexOf(row.step))
        background: Rectangle {
            radius: Theme.radius
            color: row.hovered ? Theme.hover : "transparent"
            FocusRing { control: row; radius: Theme.radius }
        }
        contentItem: RowLayout {
            spacing: 12
            Icon { iconRole: row.iconRole; size: 16; color: Theme.muted }
            Label { text: row.name; color: Theme.muted; Layout.preferredWidth: 110 }
            Label { text: row.value; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }
    component KnowRow: Rectangle {
        id: know
        required property string iconRole
        required property string sentence
        required property string keys
        radius: 6
        color: Theme.raised
        implicitHeight: 38
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: sentence + ", " + keys
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12
            Icon { iconRole: know.iconRole; size: 16 }
            Label { text: know.sentence; elide: Text.ElideRight; Layout.fillWidth: true }
            Keycap { text: know.keys }
        }
    }

    RowLayout {
        spacing: 0
        Layout.preferredWidth: 860
        Layout.preferredHeight: 535
        Layout.fillWidth: true
        Layout.fillHeight: true

        Rectangle {
            color: Theme.panel
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 15
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: 2
                ListView {
                    id: stepList
                    objectName: "gettingStartedSteps"
                    model: setup.pages
                    currentIndex: setup.page
                    interactive: false
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Steps")
                    Keys.onUpPressed: setup.goTo(setup.page - 1)
                    Keys.onDownPressed: setup.goTo(setup.page + 1)
                    delegate: ItemDelegate {
                        id: stepRow
                        required property string modelData
                        required property int index
                        readonly property bool current: index === setup.page
                        readonly property bool done: !current && setup.visited[modelData] === true
                        objectName: "gettingStartedStep_" + modelData
                        width: ListView.view.width
                        height: 30
                        focusPolicy: Qt.NoFocus
                        onClicked: setup.goTo(index)
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Step %1 of %2, %3").arg(index + 1).arg(setup.pages.length).arg(setup.stepNames[modelData])
                                         + (current ? ", " + qsTr("current") : done ? ", " + qsTr("done") : "")
                        Accessible.selected: current
                        background: Rectangle {
                            radius: Theme.radius
                            color: stepRow.current ? Theme.select : stepRow.hovered ? Theme.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Rectangle {
                                implicitWidth: 18
                                implicitHeight: 18
                                radius: 9
                                color: stepRow.current ? Theme.accent : stepRow.done ? Theme.raised : "transparent"
                                border.color: stepRow.current || stepRow.done ? "transparent" : Theme.line
                                Label {
                                    anchors.centerIn: parent
                                    text: stepRow.done ? "✓" : String(stepRow.index + 1)
                                    color: stepRow.current ? Theme.onAccent : stepRow.done ? Theme.text : Theme.muted
                                    font: setup.fontAt(10, Font.DemiBold)
                                }
                            }
                            Label {
                                text: setup.stepNames[stepRow.modelData]
                                color: stepRow.current || stepRow.done ? Theme.text : Theme.muted
                                font.weight: stepRow.current ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                Label {
                    objectName: "gettingStartedSkipNote"
                    text: qsTr("Skip setup keeps HikariSub’s defaults.")
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    font: setup.fontAt(11)
                    Layout.fillWidth: true
                }
            }
        }

        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 26
            Layout.rightMargin: 26
            Layout.topMargin: 22
            Layout.bottomMargin: 14

            Label {
                objectName: "gettingStartedTitle"
                text: pagesStack.shown ? pagesStack.shown.title : ""
                font: setup.fontAt(19, Font.DemiBold)
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
            }
            Label {
                text: pagesStack.shown ? pagesStack.shown.explanation : ""
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.bottomMargin: 16
            }
            StackLayout {
                id: pagesStack
                objectName: "gettingStartedPages"
                readonly property var shown: currentIndex >= 0 && currentIndex < children.length ? children[currentIndex] : null
                currentIndex: ["language", "import", "look", "layout", "media", "editing", "ai", "updates", "ready"].indexOf(setup.pageId)
                Layout.fillWidth: true
                Layout.fillHeight: true

                SetupPage {
                    objectName: "gettingStartedLanguage"
                    title: qsTr("Welcome to HikariSub")
                    explanation: qsTr("Let’s get started by choosing your language. Nothing is saved until Finish, and every choice can be changed later.")
                    later: qsTr("Later: Settings › General › Language")
                    laterSetting: "program.language"
                    ListView {
                        id: languageList
                        objectName: "gettingStartedLanguages"
                        model: setup.languages
                        currentIndex: setup.values["program.language"] ?? 0
                        implicitHeight: Math.min(count, 9) * 30 + 2
                        clip: true
                        keyNavigationEnabled: true
                        activeFocusOnTab: true
                        highlightMoveDuration: 0
                        Layout.preferredWidth: 380
                        Layout.preferredHeight: implicitHeight
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
                            background: Rectangle { color: languageRow.highlighted ? Theme.select : "transparent" }
                            contentItem: Label { text: languageRow.modelData; font.weight: languageRow.highlighted ? Font.DemiBold : Font.Normal }
                        }
                        Rectangle { z: -1; anchors.fill: parent; color: Theme.field; border.color: Theme.line; radius: Theme.radius }
                    }
                    Label {
                        text: qsTr("This window switches to the language you pick.")
                        color: Theme.muted
                        font: setup.fontAt(12)
                    }
                }

                SetupPage {
                    id: importPage
                    objectName: "gettingStartedImport"
                    fills: true
                    title: setup.importing ? qsTr("Choose what to bring over") : qsTr("Bring over your old settings?")
                    explanation: setup.importing
                                 ? qsTr("The next pages start from these values; a change there wins over the imported one.")
                                 : setup.foundText()
                    later: qsTr("Later: Settings › General › Import settings…")
                    RowLayout {
                        id: importChoices
                        spacing: 12
                        Layout.fillWidth: true
                        OptionCard {
                            objectName: "gettingStartedImportYes"
                            iconRole: "import"
                            text: qsTr("Import my old settings")
                            badge: qsTr("Recommended")
                            description: qsTr("Shortcuts, fonts, audio and video options and your dictionary words.")
                            chosen: setup.importing
                            onClicked: setup.importing = true
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                        }
                        OptionCard {
                            objectName: "gettingStartedImportNo"
                            iconRole: "settings"
                            text: qsTr("Set up from scratch")
                            description: qsTr("Start from HikariSub’s defaults. Settings › General › Import settings… can still bring them over later.")
                            chosen: !setup.importing
                            onClicked: setup.importing = false
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
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
                        Layout.preferredHeight: Math.max(140, importPage.availableHeight - importChoices.height - 14
                                                         - (importProblemRow.visible ? importProblemRow.height + 14 : 0)
                                                         - (encodingRow.visible ? encodingRow.height + 14 : 0))
                    }
                }

                SetupPage {
                    objectName: "gettingStartedLook"
                    title: qsTr("Look")
                    explanation: qsTr("This window changes as you choose.")
                    later: qsTr("Later: Settings › Appearance")
                    laterSetting: "appearance.theme"
                    RowLayout {
                        spacing: 24
                        CheckBox {
                            objectName: "gettingStartedFollowSystem"
                            text: qsTr("Follow system theme")
                            checked: setup.values["appearance.followSystem"] === true
                            onToggled: setup.put("appearance.followSystem", checked)
                        }
                        CheckBox {
                            objectName: "gettingStartedHighContrast"
                            text: qsTr("High contrast")
                            checked: setup.stagedThemeInfo.highContrast === true
                            onToggled: setup.put("appearance.theme", checked
                                                  ? (setup.stagedThemeInfo.dark ? "highContrastBlack" : "highContrastWhite")
                                                  : (setup.stagedThemeInfo.dark ? "dark" : "light"))
                        }
                    }
                    GroupLabel { text: qsTr("Built in") }
                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 10
                        Layout.fillWidth: true
                        Repeater {
                            model: setup.allThemes.filter(t => t.plugin.length === 0)
                            ThemeCard {
                                required property var modelData
                                objectName: "gettingStartedTheme_" + modelData.code
                                theme: modelData
                                chosen: setup.stagedTheme === modelData.code
                                onClicked: setup.chooseTheme(modelData)
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                            }
                        }
                    }
                    GroupLabel { text: qsTr("From plugins") }
                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 10
                        Layout.fillWidth: true
                        Repeater {
                            model: setup.allThemes.filter(t => t.plugin.length > 0)
                            ThemeCard {
                                required property var modelData
                                objectName: "gettingStartedPluginTheme_" + modelData.code
                                theme: modelData
                                chosen: setup.stagedTheme === modelData.code
                                onClicked: setup.chooseTheme(modelData)
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                            }
                        }
                        Card {
                            id: moreThemes
                            objectName: "gettingStartedMoreThemes"
                            text: qsTr("More themes…")
                            implicitWidth: 140
                            padding: 6
                            Accessible.role: Accessible.Button
                            Accessible.description: qsTr("Marketplace")
                            onClicked: setup.finish("marketplace:Themes")
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            contentItem: ColumnLayout {
                                spacing: 8
                                Rectangle {
                                    radius: 6
                                    color: Theme.raised
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 74
                                    Icon { anchors.centerIn: parent; iconRole: "marketplace"; size: 24 }
                                }
                                Label { text: moreThemes.text; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: qsTr("Marketplace"); color: Theme.muted; font: setup.fontAt(11) }
                            }
                        }
                        Repeater {
                            model: (4 - (setup.allThemes.filter(t => t.plugin.length > 0).length + 1) % 4) % 4
                            Item { Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        }
                    }
                    RowLayout {
                        objectName: "gettingStartedAccents"
                        visible: accents.length > 0
                        spacing: 4
                        readonly property var accents: Theme.accentsOf(setup.stagedTheme)
                        readonly property string chosen: Theme.accentChoice(setup.values["appearance.accents"] ?? [], setup.stagedTheme)
                        Label { text: qsTr("Accent colour"); Layout.preferredWidth: 110 }
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
                                onClicked: setup.put("appearance.accents", Theme.withAccentChoice(
                                    setup.values["appearance.accents"] ?? [], setup.stagedTheme, modelData.key))
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
                            Layout.leftMargin: 8
                        }
                    }
                    Label {
                        visible: setup.stagedThemeInfo.highContrast === true
                        text: qsTr("Further high contrast settings are in Settings › Appearance.")
                        color: Theme.muted
                        font: setup.fontAt(12)
                    }
                    RowLayout {
                        spacing: 4
                        Label { text: qsTr("Icons"); Layout.preferredWidth: 110 }
                        ComboBox {
                            id: iconChoice
                            objectName: "gettingStartedIcons"
                            model: setup.iconChoices
                            textRole: "text"
                            valueRole: "value"
                            Layout.preferredWidth: 300
                            Accessible.name: qsTr("Icons")
                            function refresh() { currentIndex = Math.max(0, indexOfValue(setup.values["appearance.iconTheme"] ?? "hikarisub")) }
                            Component.onCompleted: refresh()
                            onModelChanged: refresh()
                            Connections { target: setup; function onReloaded() { iconChoice.refresh() } }
                            onActivated: setup.put("appearance.iconTheme", currentValue)
                        }
                    }
                    RowLayout {
                        visible: WindowFrame.customAvailable
                        spacing: 4
                        Label { text: qsTr("Title bar"); Layout.preferredWidth: 110 }
                        SegmentedButtons {
                            objectName: "gettingStartedTitleBar"
                            choices: [{ text: qsTr("HikariSub’s"), name: "gettingStartedTitleBarOwn" },
                                      { text: qsTr("System"), name: "gettingStartedTitleBarSystem" }]
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
                            Layout.leftMargin: 8
                        }
                    }
                }

                SetupPage {
                    objectName: "gettingStartedLayout"
                    title: qsTr("How do you mostly work?")
                    explanation: qsTr("This sets the layout your first tab opens with. Leave it on Editing for a bit of everything.")
                    later: qsTr("Later: View › Layout, or the layout switcher in the title bar")
                    GridLayout {
                        columns: 3
                        columnSpacing: 10
                        rowSpacing: 10
                        Layout.fillWidth: true
                        LayoutCard { preset: ""; description: qsTr("Video, audio, the line editor and the grid.") }
                        LayoutCard { preset: "Translation"; description: qsTr("The original text above your translation, with Translation mode on.") }
                        LayoutCard { preset: "Timing"; description: qsTr("Audio across the top for timing lines by ear.") }
                        LayoutCard { preset: "Typesetting"; description: qsTr("A large video with the visual tools and the Styles panel.") }
                        LayoutCard { preset: "Karaoke"; description: qsTr("The Template editor beside the video, the audio in Karaoke mode.") }
                        LayoutCard { preset: "fullScreen"; description: qsTr("Just the picture and a line box. View › Full screen typesetting any time.") }
                    }
                }

                SetupPage {
                    objectName: "gettingStartedMedia"
                    title: qsTr("Video and audio")
                    explanation: qsTr("How subtitles are drawn on the video, and where the sound goes.")
                    later: qsTr("Later: Settings › Video and Settings › Audio")
                    laterSetting: "video.subtitleProvider"
                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10
                        Layout.fillWidth: true
                        Label { text: qsTr("Subtitles drawn by") }
                        RowLayout {
                            spacing: 12
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
                                    ? setup.app.sampleFrame(setup.values["video.subtitleProvider"] ?? 0, 420, 118) : ""
                            sourceSize: Qt.size(420, 118)
                            cache: false
                            Layout.preferredWidth: 420
                            Layout.preferredHeight: 118
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("A sample frame with a subtitle")
                        }
                        Label { text: qsTr("Sound plays on") }
                        RowLayout {
                            spacing: 12
                            PageChoice {
                                setting: "audio.outputDevice"
                                model: setup.outputDevices
                                Accessible.name: qsTr("Sound plays on")
                            }
                            IconTextButton {
                                iconRole: "volume"
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
                    later: qsTr("Later: Settings › General › Saving and Settings › Spelling and grammar")
                    laterSetting: "autosave.maxFiles"
                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10
                        Layout.fillWidth: true
                        Label { text: qsTr("Autosave files kept") }
                        PageNumber { setting: "autosave.maxFiles"; from: 2; to: 1000000; name: qsTr("Autosave files kept") }
                        Label { text: qsTr("Spelling") }
                        RowLayout {
                            spacing: 12
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
                    objectName: "gettingStartedAi"
                    title: qsTr("AI")
                    explanation: qsTr("HikariSub can help with translation, review and an agent that edits with you. It stays off unless you turn it on.")
                    later: qsTr("Later: Settings › AI")
                    laterSetting: "ai.off"
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        OptionCard {
                            objectName: "gettingStartedAi_off"
                            iconRole: "ai-assistant"
                            text: qsTr("Keep AI off")
                            description: qsTr("No AI menus, panels or downloads. Nothing about your subtitles leaves this computer.")
                            chosen: setup.values["ai.off"] !== false
                            onClicked: setup.put("ai.off", true)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                        }
                        OptionCard {
                            objectName: "gettingStartedAi_on"
                            iconRole: "ai-assistant"
                            text: qsTr("Turn AI on")
                            description: qsTr("HikariSub downloads the AI component once, after Finish. You add a provider or sign in to an agent in Settings › AI.")
                            chosen: setup.values["ai.off"] === false
                            onClicked: setup.put("ai.off", false)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                        }
                    }
                }

                SetupPage {
                    objectName: "gettingStartedUpdates"
                    title: qsTr("Updates and privacy")
                    explanation: qsTr("Which versions HikariSub offers you, whether it looks for them by itself, and whether it sends anonymous usage data.")
                    later: qsTr("Later: Settings › Updates and privacy")
                    laterSetting: "updater.checkForStable"
                    RowLayout {
                        visible: !setup.updates || setup.updates.stableOnlyOption
                        spacing: 12
                        Layout.fillWidth: true
                        OptionCard {
                            objectName: "gettingStartedStable"
                            iconRole: "check-updates"
                            text: qsTr("Stable versions only")
                            badge: setup.updates && setup.updates.prereleaseBuild ? "" : qsTr("Recommended")
                            description: qsTr("Tested releases.")
                            chosen: setup.values["updater.checkForStable"] === true
                            onClicked: setup.put("updater.checkForStable", true)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                        }
                        OptionCard {
                            objectName: "gettingStartedPrereleases"
                            iconRole: "whats-new"
                            text: qsTr("Pre-releases too")
                            description: qsTr("Beta versions as well, to try what’s coming. They can have bugs.")
                            chosen: setup.values["updater.checkForStable"] === false
                            onClicked: setup.put("updater.checkForStable", false)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        CheckBox {
                            objectName: "gettingStartedAutoCheck"
                            text: qsTr("Check for updates automatically")
                            checked: setup.values["updater.autoCheck"] === true
                            onToggled: setup.put("updater.autoCheck", checked)
                        }
                        Label {
                            text: qsTr("At most once a day, when HikariSub starts.")
                            color: Theme.muted
                            font: setup.fontAt(12)
                            Layout.leftMargin: 28
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        CheckBox {
                            objectName: "gettingStartedUsageData"
                            text: qsTr("Send anonymous usage data")
                            checked: setup.values["privacy.usageData"] === true
                            onToggled: setup.put("privacy.usageData", checked)
                        }
                        Label {
                            text: qsTr("Which features are used, errors and crashes, and how fast things open. Never your files, their names or what is in them.")
                            color: Theme.muted
                            font: setup.fontAt(12)
                            wrapMode: Text.Wrap
                            Layout.leftMargin: 28
                            Layout.fillWidth: true
                        }
                        LinkButton {
                            objectName: "gettingStartedWhatIsSent"
                            iconRole: "help"
                            text: qsTr("What is sent")
                            onClicked: setup.app.openWhatIsSent()
                            Layout.leftMargin: 24
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        CheckBox {
                            objectName: "gettingStartedDiscord"
                            text: qsTranslate("HikariSub.Options", "Show what I am doing in Discord")
                            checked: setup.values["privacy.discordPresence"] === true
                            onToggled: setup.put("privacy.discordPresence", checked)
                        }
                        Label {
                            text: qsTr("Your Discord status shows the mode you’re in. Never folders, your subtitles or the video.")
                            color: Theme.muted
                            font: setup.fontAt(12)
                            wrapMode: Text.Wrap
                            Layout.leftMargin: 28
                            Layout.fillWidth: true
                        }
                    }
                }

                SetupPage {
                    objectName: "gettingStartedReady"
                    title: qsTr("You’re set")
                    explanation: qsTr("Finish saves these choices. Everything is in Settings.")
                    RowLayout {
                        visible: setup.finishProblem.length > 0
                        spacing: 8
                        Layout.fillWidth: true
                        Icon { iconRole: "notice-warning"; size: 14; color: Theme.danger; accentColor: Theme.danger; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                        Label {
                            objectName: "gettingStartedFinishProblem"
                            text: setup.finishProblem
                            color: Theme.danger
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.AlertMessage
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        SummaryRow {
                            step: "language"
                            iconRole: "language"
                            name: qsTr("Language")
                            value: setup.languages[setup.values["program.language"] ?? 0] ?? ""
                        }
                        SummaryRow {
                            step: "look"
                            iconRole: "appearance"
                            name: qsTr("Look")
                            value: {
                                const accents = Theme.accentsOf(setup.stagedTheme)
                                const key = Theme.accentChoice(setup.values["appearance.accents"] ?? [], setup.stagedTheme)
                                const accent = accents.find(a => a.key === key)
                                const icons = setup.iconChoices.find(c => c.value === (setup.values["appearance.iconTheme"] ?? "hikarisub"))
                                const parts = [setup.stagedThemeInfo.name ?? ""]
                                if (accent)
                                    parts.push(qsTr("%1 accent").arg(qsTranslate("HikariSub.Options", accent.source)))
                                parts.push(icons && icons.value !== "hikarisub" ? qsTr("%1 icons").arg(icons.text) : qsTr("HikariSub icons"))
                                return parts.join(" · ")
                            }
                        }
                        SummaryRow {
                            step: "layout"
                            iconRole: "layout"
                            name: qsTr("Layout")
                            value: setup.layout === "fullScreen" ? qsTr("Full screen, once a video is open") : setup.layoutNames[setup.layout]
                        }
                        SummaryRow {
                            step: "ai"
                            iconRole: "ai-assistant"
                            name: qsTr("AI")
                            value: setup.values["ai.off"] !== false ? qsTr("Off") : setup.turnsAiOn ? qsTr("On, downloads after Finish") : qsTr("On")
                        }
                        SummaryRow {
                            step: "updates"
                            iconRole: "check-updates"
                            name: qsTr("Updates")
                            value: {
                                const stable = setup.values["updater.checkForStable"] !== false
                                const auto = setup.values["updater.autoCheck"] === true
                                return stable ? (auto ? qsTr("Stable only, automatically") : qsTr("Stable only, not automatically"))
                                              : (auto ? qsTr("Pre-releases, automatically") : qsTr("Pre-releases, not automatically"))
                            }
                        }
                        RowLayout {
                            objectName: "gettingStartedSummary_files"
                            spacing: 12
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            HoverHandler { id: filesHover }
                            ToolTip.text: qsTr("Everything of yours is in one folder. Copy it to move your setup to another computer.")
                            ToolTip.visible: filesHover.hovered
                            Icon { iconRole: "folder-open"; size: 16; color: Theme.muted; Layout.leftMargin: 8 }
                            Label { text: qsTr("Your files"); color: Theme.muted; Layout.preferredWidth: 110 }
                            Label {
                                objectName: "gettingStartedUserFolder"
                                text: setup.app.userFolder
                                font.family: "monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Button {
                                objectName: "gettingStartedOpenFolder"
                                flat: true
                                text: qsTr("Open folder")
                                onClicked: setup.app.openUserFolder()
                            }
                        }
                    }
                    GroupLabel { text: qsTr("Good to know"); Layout.topMargin: 8 }
                    KnowRow { iconRole: "search"; sentence: qsTr("Open anything, or run any command"); keys: setup.openAnythingKeys }
                    KnowRow { iconRole: "plugins"; sentence: qsTr("Formats, presets and themes from the marketplace"); keys: qsTr("Automation › Plugins") }
                }
            }

            Label {
                objectName: "gettingStartedLaterText"
                visible: pagesStack.shown !== null && pagesStack.shown.later.length > 0 && pagesStack.shown.laterSetting.length === 0
                text: pagesStack.shown ? pagesStack.shown.later : ""
                color: Theme.muted
                font: setup.fontAt(12)
                Layout.topMargin: 8
            }
            LinkButton {
                objectName: "gettingStartedLater"
                visible: pagesStack.shown !== null && pagesStack.shown.laterSetting.length > 0
                iconRole: "settings"
                text: pagesStack.shown ? pagesStack.shown.later : ""
                ToolTip.text: text
                ToolTip.visible: hovered
                onClicked: setup.finish("setting:" + pagesStack.shown.laterSetting)
                Layout.topMargin: 8
            }
        }
    }

    leadingActions: [
        Button {
            objectName: "gettingStartedBack"
            visible: setup.page > 0
            text: qsTr("Back")
            onClicked: setup.goTo(setup.page - 1)
        }
    ]
    buttons: [
        Button {
            objectName: "gettingStartedSkip"
            flat: true
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
