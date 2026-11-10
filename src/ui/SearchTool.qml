pragma Translator: "HikariSub.Search"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Hikari.Ui

Item {
    id: search
    objectName: "searchTool"
    required property var app
    required property int shellWindow
    property var editor: null
    readonly property alias question: question
    property bool replaceShown: false
    property string scope: "document"
    property bool optionsShown: false
    property string field: "text"
    property string lines: "all"
    property string inText: "all"
    property string matchAt: "anywhere"
    property bool matchCase: false
    property bool regex: false
    property bool includeComments: false
    property bool subfolders: false
    property bool hiddenFolders: false
    property string styles: ""
    property var finds: []
    property var replacements: []
    property var filterList: []
    property var paths: []
    property var rows: []
    property bool opened: false
    property bool resultsShown: false
    property bool resultsClosed: false
    property bool canReplaceChecked: false
    property var count: ({ total: 0, current: 0 })
    property string patternError: ""
    // MUST KILL: skipActivation makes the next activated() skip taking the editor's selection
    property bool skipActivation: false
    readonly property bool busy: app.findBusy
    readonly property bool wide: width >= 640
    readonly property bool inFolder: scope === "folder"
    readonly property bool canSearch: findText.text.length > 0 && patternError.length === 0 && !busy
    readonly property var nonDefaults: {
        const out = []
        if (scope !== "document")
            out.push(scopeChoices.find(c => c.value === scope).text)
        if (field !== "text")
            out.push(fieldChoices.find(c => c.value === field).text)
        if (scope === "document" && lines === "selected")
            out.push(qsTr("Selected lines"))
        if (scope === "document" && lines === "fromSelection")
            out.push(qsTr("From the selection"))
        if (!inFolder && styles.length > 0)
            out.push(styles.split(",").map(n => n.trim()).join(", "))
        if (inText !== "all")
            out.push(inTextChoices.find(c => c.value === inText).text)
        if (matchAt === "start")
            out.push(qsTr("Match at start"))
        if (matchAt === "end")
            out.push(qsTr("Match at end"))
        if (includeComments)
            out.push(qsTr("Comments too"))
        if (inFolder && subfolders)
            out.push(qsTr("Subfolders"))
        if (inFolder && hiddenFolders)
            out.push(qsTr("Hidden folders"))
        return out
    }
    readonly property string countText: {
        if (findText.text.length === 0 || inFolder)
            return ""
        if (patternError.length > 0 || count.total === 0)
            return qsTr("No matches")
        if (count.current > 0)
            //: The live match count: the current match, then all matches
            return qsTr("%1 of %2").arg(count.current, count.total)
        return qsTr("%n match(es)", "", count.total)
    }
    readonly property string countSpoken: count.current > 0 && patternError.length === 0
                                          ? qsTr("%1 of %n match(es)", "", count.total).arg(count.current)
                                          : countText
    readonly property int matchCount: rows.filter(r => !r.header).length
    readonly property int checkedCount: rows.filter(r => !r.header && r.checked).length
    readonly property int groupCount: rows.filter(r => r.header).length
    readonly property int lineCount: {
        const seen = new Set()
        let group = -1
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].header)
                ++group
            else
                seen.add(group + ":" + rows[i].line)
        }
        return seen.size
    }
    readonly property bool resultsInFiles: rows.some(r => !r.header && r.file)
    readonly property real optionsFixedWidth: Math.ceil(Math.max(folderChecks.implicitWidth, fieldButtons.implicitWidth,
                                                                 linesButtons.implicitWidth, inTextButtons.implicitWidth,
                                                                 matchAtButtons.implicitWidth, includeCommentsBox.implicitWidth))
    readonly property size minimumSize: Qt.size(Math.max(300, 2 * controls.Layout.margins + optionsScroll.Layout.leftMargin
                                                         + optionsScroll.leftPadding + optionsScroll.rightPadding + optionsFixedWidth),
                                                120)
    signal escaped()

    readonly property var scopeChoices: [
        { value: "document", name: "findScopeDocument", text: qsTr("This document") },
        { value: "openDocuments", name: "findScopeOpen", text: qsTr("Open documents") },
        { value: "folder", name: "findScopeFolder", text: qsTr("Folder") }
    ]
    readonly property var fieldChoices: [
        { value: "text", name: "findFieldText", text: qsTr("Text") },
        { value: "style", name: "findFieldStyle", text: qsTr("Style") },
        { value: "actor", name: "findFieldActor", text: qsTr("Actor") },
        { value: "effect", name: "findFieldEffect", text: qsTr("Effect") }
    ]
    readonly property var linesChoices: [
        { value: "all", name: "findLinesAll", text: qsTr("All") },
        { value: "selected", name: "findLinesSelected", text: qsTr("Selected") },
        { value: "fromSelection", name: "findLinesFromSelection", text: qsTr("From selected") }
    ]
    readonly property var inTextChoices: [
        { value: "all", name: "findInTextAll", text: qsTr("All text") },
        { value: "outsideTags", name: "findOutsideTags", text: qsTr("Outside tags") },
        { value: "insideTags", name: "findInsideTags", text: qsTr("Inside tags") }
    ]
    readonly property var matchAtChoices: [
        { value: "anywhere", name: "findMatchAnywhere", text: qsTr("Anywhere") },
        { value: "start", name: "findMatchStart", text: qsTr("Start") },
        { value: "end", name: "findMatchEnd", text: qsTr("End") }
    ]

    function open(withReplace) {
        if (!opened) {
            apply(app.openFindReplace())
            opened = true
        }
        if (withReplace)
            replaceShown = true
        resultsClosed = false
        refreshCount()
    }
    function focusFind() {
        findText.forceActiveFocus()
        findText.selectAll()
    }
    function activated() {
        if (skipActivation) {
            skipActivation = false
            return
        }
        if (opened)
            findText.text = app.findReplaceActivated(findText.text, shellWindow)
        refreshCount()
    }
    function save() {
        if (opened)
            app.saveFindReplaceSettings(settings())
    }
    function destroyTool() {
        save()
        opened = false
        refreshResults()
    }
    function apply(s) {
        finds = s.finds
        replacements = s.replacements
        filterList = s.filterList
        paths = s.paths
        findText.text = s.find
        replaceText.text = s.replace
        filtersText.editText = s.filters
        folderText.editText = s.folder
        styles = s.styles
        field = s.field
        lines = s.lines
        matchCase = s.matchCase
        regex = s.regex
        matchAt = s.matchAt
        inText = s.inText
        includeComments = s.includeComments
        subfolders = s.subfolders
        hiddenFolders = s.hiddenFolders
    }
    function settings() {
        return { scope: scope, replaceShown: replaceShown, find: findText.text, replace: replaceText.text,
                 styles: styles, filters: filtersText.editText, folder: folderText.editText, field: field,
                 lines: lines, matchCase: matchCase, regex: regex, matchAt: matchAt, inText: inText,
                 includeComments: includeComments, subfolders: subfolders, hiddenFolders: hiddenFolders }
    }
    function run(action) {
        if (busy || patternError.length > 0)
            return
        if (action === "findAll")
            resultsClosed = false
        app.saveFindReplaceSettings(settings())
        app.runFindReplace(action, settings())
    }
    function next() { run(inFolder ? "findAll" : "find") }
    function previous() { if (!inFolder) run("findPrevious") }
    function replaceOne() { if (inFolder) run("replaceAll"); else run("replace") }
    function refreshResults() {
        rows = app.findResults(shellWindow)
        resultsShown = app.findResultsShown(shellWindow)
        canReplaceChecked = app.canReplaceFindResults(shellWindow)
    }
    function refreshCount() {
        if (opened && visible)
            countTimer.restart()
    }
    function countNow() {
        countTimer.stop()
        const s = settings()
        patternError = app.findPatternError(s)
        const before = countSpoken
        count = findText.text.length > 0 ? app.findCount(s, shellWindow) : { total: 0, current: 0 }
        if (countSpoken !== before && countSpoken.length > 0)
            countLabel.Accessible.announce(countSpoken)
    }
    function setLines(value) {
        lines = value
        app.resetFindReplace()
    }
    function handleKey(event, inReplace) {
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
        if (enter && mods === (Qt.ControlModifier | Qt.AltModifier)) {
            if (replaceShown)
                run("replaceAll")
        } else if (enter && mods === Qt.AltModifier) {
            run("findAll")
        } else if (enter && mods === Qt.ShiftModifier) {
            previous()
        } else if (enter && mods === 0) {
            if (inReplace)
                replaceOne()
            else
                next()
        } else if (event.key === Qt.Key_C && mods === Qt.AltModifier) {
            matchCase = !matchCase
        } else if (event.key === Qt.Key_R && mods === Qt.AltModifier) {
            regex = !regex
        } else if (event.key === Qt.Key_Escape && mods === 0) {
            escaped()
        } else {
            return false
        }
        event.accepted = true
        return true
    }
    function ownsKey(event) {
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
        return (enter && [0, Qt.ShiftModifier, Qt.AltModifier, Qt.ControlModifier | Qt.AltModifier].includes(mods))
               || (mods === Qt.AltModifier && (event.key === Qt.Key_C || event.key === Qt.Key_R))
               || (event.key === Qt.Key_Escape && mods === 0)
    }

    onScopeChanged: refreshCount()
    onFieldChanged: refreshCount()
    onLinesChanged: refreshCount()
    onInTextChanged: refreshCount()
    onMatchAtChanged: refreshCount()
    onMatchCaseChanged: refreshCount()
    onRegexChanged: refreshCount()
    onIncludeCommentsChanged: refreshCount()
    onStylesChanged: refreshCount()
    onVisibleChanged: refreshCount()
    Keys.onShortcutOverride: event => event.accepted = search.ownsKey(event)
    Keys.onPressed: event => search.handleKey(event, false)

    Timer {
        id: countTimer
        interval: 150
        onTriggered: search.countNow()
    }

    Connections {
        target: search.app
        function onFindFinished(s) {
            if (!search.opened)
                return
            search.finds = s.finds
            search.replacements = s.replacements
            search.filterList = s.filterList
            search.paths = s.paths
            search.styles = s.styles
            search.countNow()
        }
        function onFindResultsChanged() { search.refreshResults() }
        function onFindQuestion(id, kind, shown, shellWindow) {
            if (shellWindow === search.shellWindow)
                question.push(id, kind, shown)
        }
        function onTabsChanged() { search.refreshCount() }
    }
    Connections {
        target: search.editor
        enabled: search.editor !== null && search.visible
        function onChanged() { search.refreshCount() }
    }

    component RecentField: TextField {
        id: recentField
        property var recent: []
        property int recentAt: -1
        selectByMouse: true
        onTextEdited: recentAt = -1
        function step(by) {
            if (recent.length === 0)
                return
            recentAt = Math.max(-1, Math.min(recent.length - 1, recentAt + by))
            if (recentAt >= 0)
                text = recent[recentAt]
        }
    }
    component Toggle: T.Button {
        id: toggle
        property string tip
        checkable: true
        focusPolicy: Qt.StrongFocus
        implicitWidth: Math.max(24, implicitContentWidth + 8)
        implicitHeight: 22
        Accessible.name: tip
        Accessible.checkable: true
        Accessible.checked: checked
        ToolTip.visible: hovered
        ToolTip.text: tip
        background: Rectangle {
            radius: 2
            color: toggle.checked ? Theme.select : toggle.hovered ? Theme.raised : "transparent"
            border.width: toggle.checked ? 1 : 0
            border.color: Theme.accent
            FocusRing { control: toggle }
        }
        contentItem: Label {
            text: toggle.text
            font.bold: true
            font.pixelSize: 11
            color: toggle.checked ? Theme.text : Theme.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    component Chip: Label {
        leftPadding: 8
        rightPadding: 8
        topPadding: 1
        bottomPadding: 1
        background: Rectangle {
            radius: height / 2
            color: Theme.raised
            border.width: 1
            border.color: Theme.line
        }
    }
    component OptionLabel: Label {
        color: Theme.muted
    }

    GridLayout {
        anchors.fill: parent
        columns: search.wide && search.resultsVisible ? 2 : 1
        columnSpacing: 0
        rowSpacing: 0

        ColumnLayout {
            id: controls
            Layout.fillWidth: true
            Layout.fillHeight: search.wide || !search.resultsVisible
            Layout.preferredWidth: search.wide ? Math.min(460, search.width * 0.5) : -1
            Layout.maximumWidth: search.wide && search.resultsVisible ? Math.max(380, search.width * 0.5) : -1
            Layout.margins: 8
            spacing: 4

            GridLayout {
                id: rowsGrid
                Layout.fillWidth: true
                columns: search.wide ? 3 : 2
                columnSpacing: 4
                rowSpacing: 4

                ToolButton {
                    id: foldButton
                    objectName: "findReplaceToggle"
                    Layout.row: 0
                    Layout.column: 0
                    focusPolicy: Qt.StrongFocus
                    checkable: true
                    checked: search.replaceShown
                    onToggled: search.replaceShown = checked
                    implicitWidth: 22
                    contentItem: Icon {
                        iconRole: search.replaceShown ? "chevron-down" : "chevron-right"
                        size: 16
                    }
                    text: qsTr("Show replace")
                    Accessible.name: qsTr("Show replace")
                    Accessible.checkable: true
                    Accessible.checked: checked
                    ToolTip.visible: hovered
                    ToolTip.text: search.replaceShown ? qsTr("Hide replace") : qsTr("Show replace")
                }

                RecentField {
                    id: findText
                    objectName: "findText"
                    Layout.row: 0
                    Layout.column: 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    recent: search.finds
                    placeholderText: qsTr("Find")
                    font.family: search.regex ? "monospace" : Qt.application.font.family
                    rightPadding: findToggles.width + 6
                    Accessible.name: qsTr("Find")
                    Accessible.description: search.patternError
                    onTextChanged: search.refreshCount()
                    Keys.onShortcutOverride: event => event.accepted = search.ownsKey(event)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Up && event.modifiers === Qt.NoModifier) {
                            findText.step(1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down && event.modifiers === Qt.NoModifier) {
                            findText.step(-1)
                            event.accepted = true
                        } else {
                            search.handleKey(event, false)
                        }
                    }
                    background: Rectangle {
                        color: Theme.field
                        radius: 2
                        border.width: findText.activeFocus || search.patternError.length > 0 ? 2 : 1
                        border.color: search.patternError.length > 0 ? Theme.danger
                                    : findText.activeFocus ? Theme.accent : Theme.line
                    }
                    Row {
                        id: findToggles
                        anchors.right: parent.right
                        anchors.rightMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Toggle {
                            objectName: "findMatchCase"
                            text: "Aa"
                            tip: qsTr("Match case")
                            checked: search.matchCase
                            onToggled: search.matchCase = checked
                        }
                        Toggle {
                            objectName: "findRegex"
                            text: ".*"
                            tip: qsTr("Regular expression")
                            checked: search.regex
                            onToggled: search.regex = checked
                        }
                    }
                }

                RowLayout {
                    id: findButtons
                    Layout.row: search.wide ? 0 : 2
                    Layout.column: search.wide ? 2 : 1
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    spacing: 2
                    Label {
                        id: countLabel
                        objectName: "findCount"
                        visible: !search.inFolder
                        text: search.countText
                        color: search.countText === qsTr("No matches") ? Theme.danger : Theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 64
                        Accessible.role: Accessible.StaticText
                        Accessible.name: search.countSpoken
                    }
                    IconToolButton {
                        objectName: "findPreviousButton"
                        visible: !search.inFolder
                        enabled: search.canSearch
                        focusPolicy: Qt.StrongFocus
                        iconRole: "match-previous"
                        text: qsTr("Find previous")
                        onClicked: search.previous()
                    }
                    IconToolButton {
                        objectName: "findButton"
                        visible: !search.inFolder
                        enabled: search.canSearch
                        focusPolicy: Qt.StrongFocus
                        iconRole: "match-next"
                        text: qsTr("Find next")
                        onClicked: search.next()
                    }
                    IconToolButton {
                        objectName: "findAllButton"
                        enabled: search.canSearch
                        focusPolicy: Qt.StrongFocus
                        iconRole: "find-in-files"
                        text: qsTr("Find all")
                        onClicked: search.run("findAll")
                    }
                    IconToolButton {
                        objectName: "findOptionsButton"
                        focusPolicy: Qt.StrongFocus
                        checkable: true
                        checked: search.optionsShown
                        onToggled: search.optionsShown = checked
                        iconRole: "filter"
                        text: qsTr("Options")
                        Accessible.checkable: true
                        Accessible.checked: checked
                        Accessible.description: search.nonDefaults.join(", ")
                        Rectangle {
                            visible: search.nonDefaults.length > 0 && !search.optionsShown
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.accent
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 3
                        }
                    }
                }

                FieldError {
                    objectName: "findPatternError"
                    Layout.row: 1
                    Layout.column: 1
                    Layout.columnSpan: search.wide ? 2 : 1
                    Layout.fillWidth: true
                    text: search.patternError
                }

                Flow {
                    objectName: "findOptionChips"
                    visible: !search.optionsShown && search.nonDefaults.length > 0
                    Layout.row: search.wide ? 2 : 3
                    Layout.column: 1
                    Layout.columnSpan: search.wide ? 2 : 1
                    Layout.fillWidth: true
                    spacing: 6
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Options that differ from the defaults")
                    Repeater {
                        model: search.nonDefaults
                        Chip {
                            required property string modelData
                            text: modelData
                            Accessible.name: modelData
                        }
                    }
                }

                RecentField {
                    id: replaceText
                    objectName: "findReplaceText"
                    visible: search.replaceShown
                    Layout.row: search.wide ? 3 : 4
                    Layout.column: 1
                    Layout.fillWidth: true
                    recent: search.replacements
                    placeholderText: qsTr("Replace with")
                    font.family: search.regex ? "monospace" : Qt.application.font.family
                    Accessible.name: qsTr("Replace with")
                    Keys.onShortcutOverride: event => event.accepted = search.ownsKey(event)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Up && event.modifiers === Qt.NoModifier) {
                            replaceText.step(1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down && event.modifiers === Qt.NoModifier) {
                            replaceText.step(-1)
                            event.accepted = true
                        } else {
                            search.handleKey(event, true)
                        }
                    }
                }

                RowLayout {
                    visible: search.replaceShown
                    Layout.row: search.wide ? 3 : 5
                    Layout.column: search.wide ? 2 : 1
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    spacing: 4
                    Button {
                        objectName: "replaceNextButton"
                        visible: !search.inFolder
                        enabled: search.canSearch
                        text: qsTr("Replace")
                        onClicked: search.run("replace")
                    }
                    Button {
                        objectName: "replaceAllButton"
                        visible: !search.inFolder
                        enabled: search.canSearch
                        text: qsTr("Replace all")
                        onClicked: search.run("replaceAll")
                    }
                    DangerButton {
                        objectName: "replaceInFilesButton"
                        visible: search.inFolder
                        enabled: search.canSearch
                        text: search.resultsInFiles ? qsTr("Replace in %n file(s)…", "", search.groupCount)
                                                    : qsTr("Replace in files…")
                        onClicked: search.run("replaceAll")
                    }
                }
            }

            ScrollView {
                id: optionsScroll
                objectName: "findOptions"
                visible: search.optionsShown
                Layout.fillWidth: true
                Layout.fillHeight: search.wide || !search.resultsVisible
                Layout.preferredHeight: search.wide || !search.resultsVisible ? -1
                                        : Math.min(optionsGrid.implicitHeight, search.height * 0.45)
                Layout.leftMargin: foldButton.width + rowsGrid.columnSpacing
                contentWidth: Math.max(availableWidth, search.optionsFixedWidth)
                leftPadding: mirrored ? effectiveScrollBarWidth : 0
                rightPadding: mirrored ? 0 : effectiveScrollBarWidth
                clip: true

                Binding {
                    target: optionsScroll.contentItem
                    property: "clip"
                    value: true
                }

                GridLayout {
                    id: optionsGrid
                    width: optionsScroll.contentWidth
                    columns: search.wide ? 2 : 1
                    columnSpacing: 8
                    rowSpacing: search.wide ? 6 : 2

                    OptionLabel { text: qsTr("Search in") }
                    SegmentedButtons {
                        objectName: "findScope"
                        visible: search.wide
                        choices: search.scopeChoices
                        currentIndex: search.scopeChoices.findIndex(c => c.value === search.scope)
                        onActivated: index => search.scope = search.scopeChoices[index].value
                        Accessible.name: qsTr("Search in")
                    }
                    ComboBox {
                        objectName: "findScopeList"
                        visible: !search.wide
                        Layout.fillWidth: true
                        model: search.scopeChoices
                        textRole: "text"
                        currentIndex: search.scopeChoices.findIndex(c => c.value === search.scope)
                        onActivated: index => search.scope = search.scopeChoices[index].value
                        Accessible.name: qsTr("Search in")
                    }

                    OptionLabel { text: qsTr("Folder"); visible: search.inFolder }
                    RowLayout {
                        visible: search.inFolder
                        Layout.fillWidth: true
                        ComboBox {
                            id: folderText
                            objectName: "findFolder"
                            editable: true
                            model: search.paths
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Folder")
                            Keys.onReturnPressed: search.run("findAll")
                        }
                        IconToolButton {
                            objectName: "findChooseFolder"
                            focusPolicy: Qt.StrongFocus
                            iconRole: "folder-open"
                            text: qsTr("Choose folder…")
                            onClicked: {
                                search.skipActivation = true
                                folderDialog.open()
                            }
                        }
                    }

                    OptionLabel { text: qsTr("Files"); visible: search.inFolder }
                    ComboBox {
                        id: filtersText
                        objectName: "findFilters"
                        visible: search.inFolder
                        editable: true
                        model: search.filterList
                        Layout.fillWidth: true
                        ToolTip.text: qsTr("File patterns separated by semicolons, such as *.ass; *.srt")
                        ToolTip.visible: hovered
                        Accessible.name: qsTr("Files")
                        Keys.onReturnPressed: search.run("findAll")
                    }

                    Item { visible: search.inFolder && search.wide; implicitWidth: 1; implicitHeight: 1 }
                    RowLayout {
                        id: folderChecks
                        visible: search.inFolder
                        CheckBox {
                            objectName: "findSubfolders"
                            text: qsTr("Subfolders")
                            checked: search.subfolders
                            onToggled: search.subfolders = checked
                        }
                        CheckBox {
                            objectName: "findHiddenFolders"
                            text: qsTr("Hidden folders")
                            checked: search.hiddenFolders
                            onToggled: search.hiddenFolders = checked
                        }
                    }

                    OptionLabel { text: qsTr("Field") }
                    SegmentedButtons {
                        id: fieldButtons
                        objectName: "findField"
                        choices: search.fieldChoices
                        currentIndex: search.fieldChoices.findIndex(c => c.value === search.field)
                        onActivated: index => search.field = search.fieldChoices[index].value
                        Accessible.name: qsTr("Field")
                    }

                    OptionLabel { text: qsTr("Lines"); visible: search.scope === "document" }
                    SegmentedButtons {
                        id: linesButtons
                        objectName: "findLines"
                        visible: search.scope === "document"
                        choices: search.linesChoices
                        currentIndex: search.linesChoices.findIndex(c => c.value === search.lines)
                        onActivated: index => search.setLines(search.linesChoices[index].value)
                        Accessible.name: qsTr("Lines")
                    }

                    OptionLabel { text: qsTr("Styles"); visible: !search.inFolder }
                    Button {
                        id: stylesButton
                        objectName: "findStyles"
                        visible: !search.inFolder
                        Layout.fillWidth: !search.wide
                        Layout.preferredWidth: search.wide ? 240 : -1
                        text: search.styles.length > 0 ? search.styles.split(",").map(n => n.trim()).join(", ")
                                                       : qsTr("All styles")
                        Accessible.name: qsTr("Styles")
                        Accessible.description: text
                        onClicked: {
                            search.skipActivation = true
                            stylesChooser.before = search.styles
                            stylesChooser.openFor(stylesButton, search.app.styleNames(),
                                                  search.styles.split(",").map(n => n.trim()).filter(n => n.length > 0),
                                                  { counts: search.app.styleLineCounts() })
                        }
                    }

                    OptionLabel { text: qsTr("In text") }
                    SegmentedButtons {
                        id: inTextButtons
                        objectName: "findInText"
                        choices: search.inTextChoices
                        currentIndex: search.inTextChoices.findIndex(c => c.value === search.inText)
                        onActivated: index => search.inText = search.inTextChoices[index].value
                        Accessible.name: qsTr("In text")
                    }

                    OptionLabel { text: qsTr("Match at") }
                    SegmentedButtons {
                        id: matchAtButtons
                        objectName: "findMatchAt"
                        choices: search.matchAtChoices
                        currentIndex: search.matchAtChoices.findIndex(c => c.value === search.matchAt)
                        onActivated: index => search.matchAt = search.matchAtChoices[index].value
                        Accessible.name: qsTr("Match at")
                    }

                    Item { visible: search.wide; implicitWidth: 1; implicitHeight: 1 }
                    CheckBox {
                        id: includeCommentsBox
                        objectName: "findIncludeComments"
                        text: qsTr("Include comments")
                        checked: search.includeComments
                        onToggled: search.includeComments = checked
                    }
                }
            }

            Item {
                visible: !search.optionsShown && (search.wide || !search.resultsVisible)
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            objectName: "findResultsPane"
            visible: search.resultsVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Results")
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                visible: !search.wide
                color: Theme.line
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                Label {
                    objectName: "findResultsSummary"
                    Layout.fillWidth: true
                    font.bold: true
                    //: %1 is the number of matches, such as "5 matches"
                    text: search.resultsInFiles
                          ? qsTr("%1 in %n file(s)", "", search.groupCount)
                                .arg(qsTr("%n match(es)", "", search.matchCount))
                          : qsTr("%1 in %n line(s)", "", search.lineCount)
                                .arg(qsTr("%n match(es)", "", search.matchCount))
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                IconToolButton {
                    objectName: "findResultsClose"
                    focusPolicy: Qt.StrongFocus
                    iconRole: "window-close"
                    text: qsTr("Close results")
                    onClicked: search.resultsClosed = true
                }
            }
            ListView {
                id: resultsList
                objectName: "findResultsList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                activeFocusOnTab: true
                model: search.rows
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Results")
                Keys.onSpacePressed: if (currentIndex >= 0) search.app.toggleFindResult(currentIndex)
                Keys.onReturnPressed: if (currentIndex >= 0) search.app.showFindResult(currentIndex)
                delegate: RowLayout {
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: resultsList.width
                    visible: modelData.header || modelData.visible
                    height: visible ? implicitHeight : 0
                    spacing: 4
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.header
                                     ? modelData.text
                                     : qsTr("Line %1: %2").arg(modelData.line, modelData.text)
                    CheckBox {
                        checked: resultRow.modelData.checked
                        Layout.leftMargin: resultRow.modelData.header ? 2 : 18
                        focusPolicy: Qt.NoFocus
                        Accessible.name: resultRow.Accessible.name
                        onClicked: search.app.toggleFindResult(resultRow.index)
                    }
                    Label {
                        visible: resultRow.modelData.header
                        text: resultRow.modelData.text
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        TapHandler { onTapped: search.app.toggleFindGroup(resultRow.index) }
                    }
                    Label {
                        visible: !resultRow.modelData.header
                        text: resultRow.modelData.header ? "" : resultRow.modelData.line
                        color: Theme.muted
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 40
                    }
                    Row {
                        visible: !resultRow.modelData.header
                        Layout.fillWidth: true
                        clip: true
                        readonly property bool showsReplacement: search.replaceShown
                        Label { text: resultRow.modelData.before || "" }
                        Label {
                            id: matchLabel
                            text: resultRow.modelData.match || ""
                            font.strikeout: parent.showsReplacement
                            background: Rectangle { color: matchLabel.palette.highlight }
                            color: matchLabel.palette.highlightedText
                        }
                        Label {
                            visible: parent.showsReplacement
                            text: replaceText.text
                            font.bold: true
                            color: Theme.accent
                        }
                        Label { text: resultRow.modelData.after || "" }
                        TapHandler { onDoubleTapped: search.app.showFindResult(resultRow.index) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                enabled: !search.busy
                IconTextButton { objectName: "findCheckAll"; iconRole: "select-all"; text: qsTr("Check all"); onClicked: search.app.checkFindResults(true) }
                IconTextButton { objectName: "findUncheckAll"; iconRole: "clear"; text: qsTr("Uncheck all"); onClicked: search.app.checkFindResults(false) }
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    id: replaceChecked
                    objectName: "replaceCheckedButton"
                    visible: search.replaceShown
                    enabled: search.canReplaceChecked && search.checkedCount > 0
                    text: qsTr("Replace %n checked", "", search.checkedCount)
                    onClicked: search.app.replaceFindResults(replaceText.text)
                }
            }
        }
    }
    readonly property bool resultsVisible: resultsShown && !resultsClosed && opened

    FolderChooser {
        id: folderDialog
        objectName: "findFolderDialog"
        title: qsTr("Choose folder")
        onAccepted: folderText.editText = search.app.localPath(selectedFolder)
    }

    ChooseStyles {
        id: stylesChooser
        objectName: "findStylesChooser"
        property string before: ""
        onChanged: (names, reverted) => search.styles = reverted ? before : names.join(",")
    }

    Item {
        id: question
        objectName: "findQuestion"
        visible: false
        property var queue: []
        property int questionId: 0
        property string kind: ""
        property string text: ""
        readonly property bool opened: boxLoader.item !== null && boxLoader.item.visible
        readonly property MessageBox box: boxLoader.item
        function choicesFor(k) {
            switch (k) {
            case "styles":
                return [{ id: "dropMissing", text: qsTr("Drop the missing styles"), role: "accept", isDefault: true, answer: "ok" },
                        { id: "everyStyle", text: qsTr("Search every style"), role: "action", answer: "yes" },
                        { id: "keepStyles", text: qsTr("Keep them"), role: "action", answer: "no" },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", answer: "cancel" }]
            case "noStyles":
                return [{ id: "everyStyle", text: qsTr("Search every style"), role: "accept", isDefault: true, answer: "yes" },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", answer: "cancel" }]
            default:
                return [{ id: "replaceInFiles", text: qsTr("Replace in the files"), role: "destructive", answer: "yes" },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true, answer: "no" }]
            }
        }
        function push(id, k, shown) {
            queue.push({id: id, kind: k, shown: shown})
            // MUST KILL: deferred so the box that asked again has closed before next() opens it
            Qt.callLater(next)
        }
        function next() {
            if (opened || queue.length === 0)
                return
            const q = queue.shift()
            questionId = q.id
            kind = q.kind
            text = q.shown.text
            search.skipActivation = true
            boxLoader.active = true
            const choices = choicesFor(q.kind)
            const id = q.id
            box.ask({ title: q.shown.title, text: q.shown.text, detail: q.shown.detail, severity: q.shown.severity,
                      choices: choices }, answer => {
                const choice = choices.find(c => c.id === answer)
                search.app.answerFindQuestion(id, choice ? choice.answer : choices.find(c => c.role === "reject").answer)
                Qt.callLater(question.next)
            })
        }
        Loader {
            id: boxLoader
            active: false
            sourceComponent: MessageBox {
                objectName: "findQuestionBox"
                settings: search.app.settings
            }
        }
    }
}
