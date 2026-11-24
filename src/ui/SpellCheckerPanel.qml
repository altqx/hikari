pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Spelling"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property var app
    required property int shellWindow
    property ToastStack notices: null
    property bool ignoreComments: false
    property bool ignoreUpperCase: false
    property bool checking: false
    onCheckingChanged: app.presenceActivity(shellWindow, "spelling", checking)
    property var current: ({})
    property var suggestions: []
    property string mode: "spelling"
    readonly property bool grammarMode: mode === "grammar"
    property var grammar: ({})
    property string grammarShown: ""
    readonly property bool found: checking && (grammarMode ? grammar.found === true : current.found === true)
    readonly property bool walking: grammarMode && checking && grammar.walking === true
    readonly property bool spellingOff: !grammarMode && checking && current.available === false && current.off === true
    readonly property bool noDictionary: !grammarMode && checking && current.available === false && !spellingOff
    readonly property Item toolbar: tools
    readonly property font labelFont: TypeRoles.sized(fontSource.font, 12, Font.Normal)
    readonly property font countFont: {
        const f = TypeRoles.sized(fontSource.font, 12, Font.Normal)
        f.features = { "tnum": 1 }
        return f
    }
    property bool detailsShown: false
    signal settingsRequested()
    signal settingRequested(string setting)

    implicitWidth: 760
    implicitHeight: 200
    readonly property real lookupWidth: wordColumn.Layout.minimumWidth + columns.columnSpacing + suggestionFrame.Layout.minimumWidth
    readonly property real lookupHeight: Math.max(wordColumn.implicitHeight, 4 * Theme.smallControlHeight + 2)
    readonly property real margins: 2 * columns.anchors.margins
    readonly property real actionsWidth: 260
    readonly property bool stacked: width > 0 && width < wordColumn.Layout.minimumWidth + 2 * columns.columnSpacing + suggestionFrame.sideBySideWidth
                                    + actionsWidth + margins
    readonly property size minimumSize: Qt.size(Math.ceil(Math.max(lookupWidth, actions.implicitWidth) + margins),
                                                Math.ceil(stacked ? lookupHeight + columns.rowSpacing + actions.implicitHeight
                                                                  : Math.max(lookupHeight, actions.implicitHeight)) + margins)

    // A FocusScope has no font of its own; the hidden fontSource Label gives the body font.
    function fontAt(size, weight) {
        return TypeRoles.sized(fontSource.font, size, weight === undefined ? Font.Normal : weight)
    }
    function summary(state) {
        const t = state.tally ?? {}
        const done = []
        if ((t.replaced ?? 0) > 0)
            done.push(qsTr("%n word(s) replaced", "", t.replaced))
        if ((t.ignored ?? 0) > 0)
            //: After "3 words replaced, ": how many misspelled words were ignored.
            done.push(qsTr("%n ignored", "", t.ignored))
        if ((t.added ?? 0) > 0)
            //: How many words were added to the dictionary.
            done.push(qsTr("%n added to the dictionary", "", t.added))
        const checked = qsTr("%n lines checked", "", state.checked ?? 0)
        //: Joins two counts, as in "3 words replaced, 2 ignored".
        return done.length > 0 ? checked + " · " + done.reduce((a, b) => qsTr("%1, %2").arg(a, b)) : checked
    }
    function options() {
        return {ignoreComments: ignoreComments, ignoreUpperCase: ignoreUpperCase}
    }
    function showGrammar(next, action) {
        grammar = next
        if (next.problem && next.problem.length > 0 && notices)
            notices.post("error", action || qsTr("Grammar check"), next.problem)
        const key = next.found ? [next.index, next.line, next.rule, next.before, next.text].join("\u0001") : ""
        if (key === grammarShown)
            return
        grammarShown = key
        suggestions = next.found ? next.suggestions : []
        suggestionList.currentIndex = suggestions.length > 0 ? 0 : -1
        replacement.text = next.found ? next.replacement : ""
        if (next.found && !replacement.activeFocus && panel.activeFocus)
            replacement.forceActiveFocus()
    }
    function setMode(next) {
        if (next === mode)
            return
        const wasChecking = checking
        stop()
        mode = next
        if (wasChecking)
            start()
    }
    function show(next, action) {
        if (next.unchanged)
            return
        current = next
        if (next.problem && next.problem.length > 0) {
            if (notices)
                notices.post("error", action || qsTr("Replace"), next.problem)
            return
        }
        suggestions = next.suggestions
        suggestionList.currentIndex = suggestions.length > 0 ? 0 : -1
        replacement.text = next.replacement
    }
    function start() {
        checking = true
        detailsShown = false
        if (grammarMode) {
            grammarShown = ""
            showGrammar(app.startGrammarWalk(shellWindow))
        } else {
            show(app.openSpellChecker(options(), shellWindow))
        }
        if (found)
            replacement.forceActiveFocus()
        else if (emptyAction.visible)
            emptyAction.forceActiveFocus()
    }
    function stop() {
        if (!checking)
            return
        checking = false
        current = ({})
        grammar = ({})
        grammarShown = ""
        suggestions = []
        app.closeSpellChecker(shellWindow)
        app.closeGrammarWalk(shellWindow)
    }
    function setIgnoreComments(on) { ignoreComments = on }
    function setIgnoreUpperCase(on) { ignoreUpperCase = on }
    function replace() {
        if (grammarMode)
            showGrammar(app.grammarWalkReplace(replacement.text), qsTr("Replace"))
        else
            show(app.spellCheckerReplace(replacement.text, options()), qsTr("Replace"))
    }
    function replaceAll() { show(app.spellCheckerReplaceAll(current.word, replacement.text, options()), qsTr("Replace all")) }
    function ignore() {
        if (grammarMode)
            showGrammar(app.grammarWalkIgnore())
        else
            show(app.spellCheckerIgnore(options()))
    }
    function ignoreRule() {
        const next = app.grammarWalkIgnoreRule()
        const group = next.ignoredGroup ?? -1
        showGrammar(next)
        if (group < 0 || !notices)
            return
        const groups = app.grammarGroups()
        notices.post("info", qsTr("%1 turned off").arg(next.ignoredGroupName),
                     qsTr("Grammar check no longer uses its %n rules. Turn it back on in Settings › Spelling and grammar.", "",
                          group < groups.length ? groups[group].rules : 0),
                     {actions: [{text: qsTr("Undo"), run: () => panel.app.setGrammarGroup(group, true)},
                                {text: qsTr("Grammar settings…"), run: () => panel.settingsRequested()}]})
    }
    function ignoreAll() { show(app.spellCheckerIgnoreAll(current.word, options())) }
    function addWord() {
        dictionaryTier.remembered = dictionaryTier.readTier()
        const project = dictionaryTier.tier === "project"
        if (grammarMode)
            showGrammar(app.grammarWalkAddWord(project), qsTr("Add to dictionary"))
        else
            show(app.spellCheckerAddWord(current.word, Object.assign(options(), {project: project})))
    }
    function chooseLanguage(symbol) {
        if (symbol === app.dictionaryLanguage)
            return
        app.chooseDictionaryLanguage(symbol, shellWindow)
        if (checking && !grammarMode)
            show(app.openSpellChecker(options(), shellWindow))
    }
    function openAddedWords() {
        addedWordsLoader.active = true
        addedWordsLoader.item.openWith(app.addedDictionaryWords())
    }

    // MUST KILL: actionPressed scans for a pressed action so focus-in does not swap the word under a click
    function actionPressed() {
        if ([replaceButton, replaceAllButton, ignoreButton, ignoreAllButton, addWordButton, emptyAction].some(b => b.pressed))
            return true
        const delegates = suggestionList.contentItem.children
        for (let i = 0; i < delegates.length; ++i)
            if (delegates[i].pressed === true)
                return true
        return false
    }
    function activate() {
        if (!activeFocus || !checking || grammarMode || actionPressed())
            return
        const next = app.spellCheckerActivated(options())
        if (next.restarted)
            show(next)
    }
    onActiveFocusChanged: if (activeFocus) Qt.callLater(activate)
    // MUST KILL: restarting and restartPending dedupe the restart signals openSpellChecker and restartSpellChecker emit
    property bool restarting: false
    property bool restartPending: false
    function scheduleRestart() {
        if (restartPending)
            return
        restartPending = true
        Qt.callLater(restartCheck)
    }
    function restartCheck() {
        restartPending = false
        if (!checking || grammarMode)
            return
        restarting = true
        show(app.openSpellChecker(options(), shellWindow))
        restarting = false
    }
    Connections {
        target: panel.app
        function onSpellCheckerRestarted() {
            if (panel.checking && !panel.restarting && !panel.grammarMode)
                panel.scheduleRestart()
        }
        function onSpellingChanged() {
            if (panel.checking && !panel.restarting && !panel.grammarMode && panel.current.available === false)
                panel.scheduleRestart()
        }
        function onGrammarWalkChanged() {
            if (panel.checking && panel.grammarMode)
                panel.showGrammar(panel.app.grammarWalkState(panel.shellWindow))
        }
        function onGrammarChanged() {
            if (panel.grammarMode && panel.checking && panel.grammar.on !== panel.app.grammarOn)
                panel.start()
        }
    }
    component QuietButton: Button {
        flat: true
        implicitHeight: Theme.controlHeight
        topPadding: 0
        bottomPadding: 0
    }
    component ActionButton: Button {
        implicitHeight: Theme.controlHeight
        topPadding: 0
        bottomPadding: 0
        Layout.fillWidth: true
        Layout.minimumWidth: implicitWidth
    }

    TextMetrics {
        id: statusMetrics
        font: panel.countFont
        text: qsTr("Line %1 of %2 · %n found so far", "", 888).arg(8888, 8888)
    }
    Item {
        id: tools
        objectName: "spellTools"
        visible: parent !== null && parent.objectName === "dockToolbarSlot"
        readonly property Item host: parent ? parent.parent : null
        // MUST KILL: room copies DockTabBar's toolbar slot sizing (naturalTabsWidth, 4 margin + 8 gap) from outside it
        readonly property real room: host && host.naturalTabsWidth !== undefined ? host.width - host.naturalTabsWidth - 12 : Infinity
        readonly property real smallWidth: modeSwitch.implicitWidth + (cancel.visible ? 8 + cancel.implicitWidth : 0)
        readonly property real languageWidth: panel.grammarMode ? 0 : 8 + language.Layout.preferredWidth
        readonly property real statusWidth: 8 + Math.ceil(statusMetrics.advanceWidth)
        readonly property bool showsLanguage: room >= smallWidth + languageWidth
        readonly property bool showsStatus: room >= smallWidth + languageWidth + statusWidth
        implicitWidth: smallWidth + (showsLanguage ? languageWidth : 0) + (showsStatus ? statusWidth : 0)
        implicitHeight: Theme.headerHeight
        RowLayout {
            id: toolRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            SegmentedButtons {
                id: modeSwitch
                objectName: "spellMode"
                implicitHeight: Theme.controlHeight
                choices: [{text: qsTr("Spelling"), name: "spellModeSpelling"}, {text: qsTr("Grammar"), name: "spellModeGrammar"}]
                currentIndex: panel.grammarMode ? 1 : 0
                onActivated: index => panel.setMode(index === 1 ? "grammar" : "spelling")
                Accessible.name: qsTr("Check")
            }
            ComboBox {
                id: language
                objectName: "spellLanguage"
                visible: !panel.grammarMode && tools.showsLanguage
                implicitHeight: Theme.controlHeight
                font: panel.labelFont
                Layout.preferredWidth: 140
                textRole: "name"
                valueRole: "symbol"
                model: []
                displayText: currentIndex >= 0 ? currentText : panel.current.language ?? panel.app.dictionaryLanguage
                Accessible.name: qsTr("Dictionary language")
                onActivated: index => panel.chooseLanguage(valueAt(index))
                function refresh() {
                    model = panel.app.dictionaryChoices()
                    follow()
                }
                function follow() { currentIndex = indexOfValue(panel.app.dictionaryLanguage) }
                Component.onCompleted: refresh()
                popup.onAboutToShow: refresh()
                Connections {
                    target: panel.app
                    function onSpellingChanged() { language.follow() }
                }
            }
            Label {
                id: status
                objectName: "spellStatus"
                visible: tools.showsStatus
                Layout.fillWidth: true
                font: panel.countFont
                color: Theme.muted
                elide: Text.ElideRight
                text: panel.walking
                      ? qsTr("Checking · line %1 of %2").arg(panel.grammar.checked ?? 0, panel.grammar.total ?? 0)
                      : !panel.found ? ""
                      : panel.grammarMode
                      ? qsTr("Line %1 of %2 · %n found so far", "", panel.grammar.count ?? 0)
                            .arg(panel.grammar.line ?? 0, panel.grammar.total ?? 0)
                      : qsTr("Line %1 of %2 · %n found so far", "", panel.current.tally?.found ?? 0)
                            .arg(panel.current.line ?? 0, panel.current.lines ?? 0)
            }
            QuietButton {
                id: cancel
                objectName: "grammarCancel"
                visible: panel.walking
                text: qsTr("Cancel")
                onClicked: panel.showGrammar(panel.app.cancelGrammarWalk())
            }
        }
    }

    Rectangle {
        objectName: "spellProgress"
        visible: panel.walking
        height: 2
        width: parent.width * Math.min(1, (panel.grammar.checked ?? 0) / Math.max(1, panel.grammar.total ?? 1))
        color: Theme.accent
    }

    ColumnLayout {
        id: emptyState
        objectName: "spellEmpty"
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 16)
        visible: !panel.found && !panel.walking
        spacing: 8
        readonly property bool grammarOff: panel.grammarMode && !panel.app.grammarOn
        readonly property bool finished: panel.checking && !panel.noDictionary && !panel.spellingOff && !grammarOff
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            spacing: 8
            Icon {
                objectName: "spellNoDictionaryMark"
                visible: panel.noDictionary
                iconRole: "notice-warning"
                color: Theme.danger
                accentColor: Theme.danger
            }
            Label {
                objectName: "spellEmptyText"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font: panel.checking || emptyState.grammarOff ? panel.fontAt(13, Font.DemiBold) : fontSource.font
                color: panel.checking || emptyState.grammarOff ? Theme.text : Theme.muted
                text: panel.grammarMode
                    ? (emptyState.grammarOff ? qsTr("Grammar check is off")
                       : !panel.checking ? qsTr("Checks the grammar of the whole file from the active line")
                       : panel.grammar.cancelled === true ? qsTr("Grammar check cancelled")
                       : qsTr("No more grammar findings"))
                    : !panel.checking ? qsTr("Checks the spelling from the active line")
                    : panel.spellingOff ? qsTr("Spell checking is turned off.")
                    : panel.noDictionary
                    ? ((panel.current.language ?? "").length > 0
                       ? qsTr("No dictionary for %1 was found, so nothing can be checked.").arg(panel.current.language)
                       : qsTr("No dictionary was found, so nothing can be checked."))
                    : qsTr("No more spelling mistakes")
            }
        }
        Label {
            objectName: "spellEmptyDetail"
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            visible: text.length > 0
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font: panel.countFont
            color: Theme.muted
            text: emptyState.grammarOff ? qsTr("Choose an English in Settings to check grammar with Harper.")
                : panel.noDictionary ? (panel.detailsShown
                                        ? qsTr("Dictionaries are .dic and .aff files in “%1”.").arg(panel.current.folder ?? "")
                                        : "")
                : !emptyState.finished ? ""
                : panel.grammarMode ? (panel.grammar.done === true ? qsTr("%n lines checked", "", panel.grammar.checked ?? 0) : "")
                : panel.summary(panel.current)
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Button {
                id: emptyAction
                objectName: "spellEmptyAction"
                implicitHeight: Theme.controlHeight
                topPadding: 0
                bottomPadding: 0
                flat: emptyState.finished
                readonly property bool settings: emptyState.grammarOff
                text: panel.noDictionary ? qsTr("Choose a language…")
                    : panel.spellingOff ? qsTr("Open Settings…")
                    : settings ? qsTr("Grammar settings…")
                    : panel.checking ? qsTr("Check again")
                    : panel.grammarMode ? qsTr("Check grammar") : qsTr("Check spelling")
                onClicked: panel.noDictionary ? panel.settingRequested("editor.dictionaryLanguage")
                         : panel.spellingOff ? panel.settingRequested("editor.spellchecker")
                         : settings ? panel.settingsRequested() : panel.start()
            }
            QuietButton {
                objectName: "spellEmptyDetails"
                visible: panel.noDictionary
                text: qsTr("Details")
                checkable: true
                checked: panel.detailsShown
                onToggled: panel.detailsShown = checked
            }
        }
    }

    GridLayout {
        id: columns
        objectName: "spellColumns"
        anchors.fill: parent
        anchors.margins: 8
        columns: panel.stacked ? 2 : 3
        columnSpacing: 24
        rowSpacing: 8
        visible: panel.found
        ColumnLayout {
            id: wordColumn
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.minimumWidth: 220
            Layout.preferredWidth: 330
            spacing: 4
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Label {
                    id: misspell
                    objectName: "spellMisspell"
                    text: (panel.grammarMode ? panel.grammar.text : panel.current.word) ?? ""
                    textFormat: Text.PlainText
                    font: panel.fontAt(20, Font.DemiBold)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    Layout.alignment: Qt.AlignBaseline
                    Accessible.name: panel.grammarMode ? text : qsTr("Misspelled word: %1").arg(text)
                }
                Label {
                    id: lineLabel
                    objectName: panel.grammarMode ? "grammarLine" : "spellLine"
                    visible: ((panel.grammarMode ? panel.grammar.line : panel.current.line) ?? 0) > 0
                    text: panel.grammarMode ? qsTr("Line %1 · %2").arg(panel.grammar.line ?? 0, panel.grammar.groupName ?? "")
                                            : qsTr("Line %1").arg(panel.current.line ?? 0)
                    textFormat: Text.PlainText
                    color: Theme.muted
                    font: panel.countFont
                    Layout.alignment: Qt.AlignBaseline
                }
            }
            Label {
                objectName: "grammarMessage"
                visible: panel.grammarMode
                text: panel.grammar.message ?? ""
                textFormat: Text.PlainText
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Accessible.name: text
                ToolTip.text: text
                ToolTip.visible: truncated && hover.hovered
                HoverHandler { id: hover }
            }
            Label {
                objectName: "spellContext"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                elide: Text.ElideRight
                color: Theme.muted
                textFormat: Text.StyledText
                readonly property var shown: panel.grammarMode
                    ? {before: panel.grammar.before, word: panel.grammar.text, after: panel.grammar.after, colour: Theme.grammar}
                    : {before: panel.current.before, word: panel.current.word, after: panel.current.after, colour: Theme.text}
                text: Qt.escapeHtml(shown.before ?? "") + "<font color=\"" + shown.colour + "\">"
                      + Qt.escapeHtml(shown.word ?? "") + "</font>" + Qt.escapeHtml(shown.after ?? "")
                Accessible.name: (shown.before ?? "") + (shown.word ?? "") + (shown.after ?? "")
            }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Label {
                    text: qsTr("Replace with")
                    font: panel.labelFont
                    color: Theme.muted
                }
                TextField {
                    id: replacement
                    objectName: "spellReplacement"
                    implicitHeight: Theme.controlHeight
                    topPadding: 0
                    bottomPadding: 0
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Replace with")
                    Keys.onReturnPressed: panel.replace()
                    Keys.onEnterPressed: panel.replace()
                }
            }
        }
        Frame {
            id: suggestionFrame
            Layout.fillHeight: true
            Layout.minimumHeight: panel.stacked ? panel.lookupHeight : 0
            Layout.minimumWidth: 160
            Layout.preferredWidth: panel.stacked ? Layout.minimumWidth : sideBySideWidth
            readonly property int sideBySideWidth: 220
            padding: 1
            background: Rectangle { color: Theme.field; border.color: Theme.line; radius: Theme.radius }
            Label {
                anchors.centerIn: parent
                visible: suggestionList.count === 0
                font: panel.labelFont
                color: Theme.muted
                text: qsTr("No suggestions")
            }
            ListView {
                id: suggestionList
                objectName: "spellSuggestions"
                model: panel.suggestions
                clip: true
                anchors.fill: parent
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Suggestions")
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: suggestion
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    implicitHeight: Theme.smallControlHeight
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    focusPolicy: Qt.ClickFocus
                    background: Rectangle {
                        color: suggestion.highlighted ? Theme.select : "transparent"
                        Rectangle {
                            anchors.fill: parent
                            visible: suggestion.hovered || suggestion.down
                            color: suggestion.down ? Theme.pressed : Theme.hover
                        }
                    }
                    onClicked: {
                        suggestionList.currentIndex = index
                        replacement.text = modelData
                    }
                    onDoubleClicked: {
                        replacement.text = modelData
                        panel.replace()
                    }
                }
            }
        }
        GridLayout {
            id: actions
            objectName: "spellActions"
            Layout.alignment: Qt.AlignTop
            Layout.columnSpan: panel.stacked ? 2 : 1
            Layout.fillWidth: panel.stacked
            Layout.preferredWidth: panel.stacked ? -1 : panel.actionsWidth
            columns: panel.stacked ? 3 : 2
            uniformCellWidths: true
            rowSpacing: 8
            columnSpacing: 8
            PrimaryButton {
                id: replaceButton
                objectName: "spellReplace"
                text: qsTr("Replace")
                Layout.fillWidth: true
                Layout.minimumWidth: implicitWidth
                onClicked: panel.replace()
            }
            ActionButton {
                id: replaceAllButton
                objectName: "spellReplaceAll"
                visible: !panel.grammarMode
                text: qsTr("Replace all")
                onClicked: panel.replaceAll()
            }
            ActionButton {
                id: ignoreButton
                objectName: "spellIgnore"
                text: qsTr("Ignore")
                onClicked: panel.ignore()
            }
            ActionButton {
                id: ignoreAllButton
                objectName: "spellIgnoreAll"
                visible: !panel.grammarMode
                text: qsTr("Ignore all")
                onClicked: panel.ignoreAll()
            }
            ActionButton {
                id: ignoreRuleButton
                objectName: "grammarIgnoreRule"
                visible: panel.grammarMode
                text: qsTr("Ignore this rule")
                Layout.columnSpan: panel.stacked ? 1 : 2
                onClicked: panel.ignoreRule()
            }
            RowLayout {
                spacing: 1
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.minimumWidth: implicitWidth
                IconTextButton {
                    id: addWordButton
                    objectName: "spellAddWord"
                    iconRole: "dictionary-add"
                    text: !dictionaryTier.hasProject ? qsTr("Add to dictionary") : dictionaryTier.saveText
                    enabled: !panel.grammarMode || panel.grammar.word === true
                    implicitHeight: Theme.controlHeight
                    topPadding: 0
                    bottomPadding: 0
                    Layout.fillWidth: true
                    onClicked: panel.addWord()
                }
                IconToolButton {
                    id: addWordArrow
                    objectName: "spellAddWordArrow"
                    visible: dictionaryTier.hasProject
                    enabled: addWordButton.enabled
                    iconRole: "chevron-down"
                    iconSize: 12
                    implicitWidth: 18
                    text: qsTr("Where to add")
                    Accessible.role: Accessible.ButtonMenu
                    onClicked: dictionaryTier.popup(addWordArrow, 0, addWordArrow.height)
                }
                TierSaveMenu {
                    id: dictionaryTier
                    objectName: "spellAddWordMenu"
                    itemPrefix: "spellAddWord"
                    listId: "dictionary"
                    settings: panel.app.settings
                    hasProject: panel.app.projectName(panel.shellWindow).length > 0
                    projectText: qsTr("Add to project dictionary")
                    mineText: qsTr("Add to my dictionary")
                }
            }
        }
    }

    Label { id: fontSource; visible: false }
    Loader {
        id: addedWordsLoader
        active: false
        sourceComponent: SpellingAddedWords {
            settings: panel.app.settings
            onRemoveRequested: words => panel.app.removeDictionaryWords(words)
        }
    }
}
