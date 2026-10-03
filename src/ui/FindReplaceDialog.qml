// F1: legacy FindReplaceDialog (GLOBAL_SEARCH Ctrl+F, GLOBAL_FIND_REPLACE
// Ctrl+H): the Find, Find and replace and Find in subtitles tabs over one
// FindReplace, the "Search results" dialog, and the legacy message boxes,
// which the application waits for (findQuestion / answerFindQuestion).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts

Item {
    id: findReplace
    objectName: "findReplace"
    required property var app
    readonly property alias dialog: dialog
    property int tab: 0
    property var finds: []
    property var replacements: []
    property var filterList: []
    property var paths: []
    property var rows: []
    property bool opened: false

    // Ctrl+F / Ctrl+H: legacy ShowDialog, or the dialog's own accelerator
    // (the same tab again hides it).
    function activate(which) {
        if (dialog.visible && dialog.activeFocus && which === tab) {
            dialog.close()
            return
        }
        showTab(which)
    }
    function showTab(which) {
        if (!opened) {
            apply(app.findReplaceSettings(which))
            opened = true
        } else if (which !== tab) {
            apply(app.switchFindReplaceTab(settings(), which))
        }
        tab = which
        tabs.currentIndex = which
        dialog.open()
        // OnActivate: the text selected in the editor.
        findText.editText = app.findReplaceActivated(findText.editText)
        findText.forceActiveFocus()
    }
    function apply(s) {
        finds = s.finds
        replacements = s.replacements
        filterList = s.filterList
        paths = s.paths
        findText.editText = s.find
        replaceText.editText = s.replace
        filtersText.editText = s.filters
        folderText.editText = s.folder
        styles.text = s.styles
        fields.itemAt(s.field).checked = true
        for (let i = 0; i < lineButtons.count; ++i)
            lineButtons.itemAt(i).checked = i === s.lines
        matchCase.checked = s.matchCase
        regex.checked = s.regex
        startOfText.checked = s.startOfText
        endOfText.checked = s.endOfText
        includeComments.checked = s.includeComments
        skipTags.checked = s.skipTags
        skipText.checked = s.skipText
        if (!opened) {
            subfolders.checked = s.subfolders
            hiddenFolders.checked = s.hiddenFolders
        }
    }
    function settings() {
        let field = 0, lines = 3
        for (let i = 0; i < fields.count; ++i) if (fields.itemAt(i).checked) field = i
        for (let i = 0; i < lineButtons.count; ++i) if (lineButtons.itemAt(i).checked) lines = i
        return {tab: tab, find: findText.editText, replace: replaceText.editText, styles: styles.text,
                filters: filtersText.editText, folder: folderText.editText, field: field, lines: lines,
                matchCase: matchCase.checked, regex: regex.checked, startOfText: startOfText.checked,
                endOfText: endOfText.checked, includeComments: includeComments.checked,
                skipTags: skipTags.checked, skipText: skipText.checked, subfolders: subfolders.checked,
                hiddenFolders: hiddenFolders.checked}
    }
    function run(action) {
        const s = app.runFindReplace(action, settings())
        finds = s.finds
        replacements = s.replacements
        filterList = s.filterList
        paths = s.paths
        // AddRecent selects the first entry of each list again.
        findText.editText = s.find
        replaceText.editText = s.replace
        filtersText.editText = s.filters
        folderText.editText = s.folder
        styles.text = s.styles
        if (action === "findAllCurrent" || action === "findAllTabs" || action === "findInFiles")
            results.openResults()
    }
    // Enter (OnEnterConfirm): Replace from the replacement field, else Find.
    function confirm() {
        if (tab !== 0 && replaceText.activeFocus)
            run(tab === 2 ? "replaceInFiles" : "replace")
        else
            run(tab === 2 ? "findInFiles" : "find")
    }

    Dialog {
        id: dialog
        objectName: "findReplaceDialog"
        title: findReplace.tab === 0 ? qsTr("Find") : findReplace.tab === 1 ? qsTr("Find and replace")
                                                                           : qsTr("Find in subtitles")
        modal: false
        anchors.centerIn: parent
        onClosed: findReplace.app.saveFindReplaceSettings(findReplace.settings())

        ColumnLayout {
            anchors.fill: parent
            TabBar {
                id: tabs
                objectName: "findTabs"
                Layout.fillWidth: true
                TabButton { text: qsTr("Find") }
                TabButton { text: qsTr("Find and replace") }
                TabButton { text: qsTr("Find in subtitles") }
                onCurrentIndexChanged: {
                    if (findReplace.opened && currentIndex !== findReplace.tab) {
                        findReplace.apply(findReplace.app.switchFindReplaceTab(findReplace.settings(), currentIndex))
                        findReplace.tab = currentIndex
                    }
                }
            }
            RowLayout {
                ColumnLayout {
                    GridLayout {
                        columns: 2
                        Label { text: qsTr("Search text:") }
                        ComboBox {
                            id: findText
                            objectName: "findText"
                            editable: true
                            model: findReplace.finds
                            Layout.fillWidth: true
                            Layout.minimumWidth: 276
                            Accessible.name: qsTr("Search text:")
                            Keys.onReturnPressed: findReplace.confirm()
                            Keys.onEnterPressed: findReplace.confirm()
                        }
                        Label { text: qsTr("Replace with:"); visible: findReplace.tab !== 0 }
                        ComboBox {
                            id: replaceText
                            objectName: "findReplaceText"
                            editable: true
                            visible: findReplace.tab !== 0
                            model: findReplace.replacements
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Replace with:")
                            Keys.onReturnPressed: findReplace.confirm()
                            Keys.onEnterPressed: findReplace.confirm()
                        }
                        Label { text: qsTr("Filters:"); visible: findReplace.tab === 2 }
                        ComboBox {
                            id: filtersText
                            objectName: "findFilters"
                            editable: true
                            visible: findReplace.tab === 2
                            model: findReplace.filterList
                            Layout.fillWidth: true
                            ToolTip.text: qsTr("Windows search filters separated by semicolons, e.g. \"*.ass; *.srt\".")
                            ToolTip.visible: hovered
                            Accessible.name: qsTr("Filters:")
                            Keys.onReturnPressed: findReplace.confirm()
                        }
                        Label { text: qsTr("Catalog:"); visible: findReplace.tab === 2 }
                        RowLayout {
                            visible: findReplace.tab === 2
                            ComboBox {
                                id: folderText
                                objectName: "findFolder"
                                editable: true
                                model: findReplace.paths
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Subtitle search folder:")
                                Keys.onReturnPressed: findReplace.confirm()
                            }
                            Button {
                                text: " ... "
                                Accessible.name: qsTr("Choose save folder")
                                onClicked: folderDialog.open()
                            }
                        }
                    }
                    RowLayout {
                        ColumnLayout {
                            CheckBox { id: matchCase; objectName: "findMatchCase"; text: qsTr("Match case") }
                            CheckBox { id: regex; objectName: "findRegex"; text: qsTr("Regular expressions") }
                            // OnRecheck: Beginning and End of text exclude each other.
                            CheckBox {
                                id: startOfText
                                objectName: "findStartOfText"
                                text: qsTr("Beginning of text")
                                onToggled: if (checked) endOfText.checked = false
                            }
                            CheckBox {
                                id: endOfText
                                objectName: "findEndOfText"
                                text: qsTr("End of text")
                                onToggled: if (checked) startOfText.checked = false
                            }
                        }
                        ColumnLayout {
                            CheckBox { id: includeComments; objectName: "findIncludeComments"; text: qsTr("Include comments") }
                            CheckBox {
                                id: skipTags
                                objectName: "findSkipTags"
                                text: qsTr("Skip tags")
                                onToggled: if (checked) skipText.checked = false
                            }
                            CheckBox {
                                id: skipText
                                objectName: "findSkipText"
                                text: qsTr("Skip text")
                                onToggled: if (checked) skipTags.checked = false
                            }
                        }
                        GroupBox {
                            title: qsTr("In field")
                            GridLayout {
                                columns: 2
                                ButtonGroup { id: fieldGroup }
                                Repeater {
                                    id: fields
                                    model: [qsTr("Text"), qsTr("Styles"), qsTr("Actor"), qsTr("Effect")]
                                    RadioButton { text: modelData; ButtonGroup.group: fieldGroup }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Button {
                        objectName: "findButton"
                        text: qsTr("Find")
                        visible: findReplace.tab !== 2
                        Layout.fillWidth: true
                        onClicked: findReplace.run("find")
                    }
                    Button {
                        objectName: "findAllTabsButton"
                        text: qsTr("Find in all open\nsubtitles")
                        visible: findReplace.tab === 0
                        Layout.fillWidth: true
                        onClicked: findReplace.run("findAllTabs")
                    }
                    Button {
                        objectName: "findAllCurrentButton"
                        text: qsTr("Find all\nin current subtitles")
                        visible: findReplace.tab === 0
                        Layout.fillWidth: true
                        onClicked: findReplace.run("findAllCurrent")
                    }
                    Button {
                        objectName: "replaceNextButton"
                        text: qsTr("Replace next")
                        visible: findReplace.tab === 1
                        Layout.fillWidth: true
                        onClicked: findReplace.run("replace")
                    }
                    Button {
                        objectName: "replaceAllButton"
                        text: qsTr("Replace all")
                        visible: findReplace.tab === 1
                        Layout.fillWidth: true
                        onClicked: findReplace.run("replaceAll")
                    }
                    Button {
                        objectName: "replaceAllTabsButton"
                        text: qsTr("Replace in all open\nsubtitles")
                        visible: findReplace.tab === 1
                        Layout.fillWidth: true
                        onClicked: findReplace.run("replaceAllTabs")
                    }
                    Button {
                        objectName: "findInFilesButton"
                        text: qsTr("Find in subtitles")
                        visible: findReplace.tab === 2
                        Layout.fillWidth: true
                        onClicked: findReplace.run("findInFiles")
                    }
                    Button {
                        objectName: "replaceInFilesButton"
                        text: qsTr("Replace in subtitles")
                        visible: findReplace.tab === 2
                        Layout.fillWidth: true
                        onClicked: findReplace.run("replaceInFiles")
                    }
                    Button {
                        text: qsTr("Close")
                        Layout.fillWidth: true
                        onClicked: dialog.close()
                    }
                    CheckBox { id: subfolders; objectName: "findSubfolders"; text: qsTr("Search in subfolders"); visible: findReplace.tab === 2 }
                    CheckBox { id: hiddenFolders; objectName: "findHiddenFolders"; text: qsTr("Search in hidden\nfolders"); visible: findReplace.tab === 2 }
                }
            }
            GroupBox {
                title: qsTr("Lines")
                visible: findReplace.tab !== 2
                Layout.fillWidth: true
                RowLayout {
                    ButtonGroup { id: linesGroup }
                    Repeater {
                        id: lineButtons
                        model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From selected")]
                        // TabWindow::Reset: the next search starts over.
                        RadioButton { text: modelData; ButtonGroup.group: linesGroup; onClicked: findReplace.app.resetFindReplace() }
                    }
                    Button {
                        objectName: "findChooseStyles"
                        text: "+"
                        Accessible.name: qsTr("Choose styles")
                        onClicked: stylesDialog.openWith(findReplace.app.styleNames())
                    }
                    TextField {
                        id: styles
                        objectName: "findStyles"
                        Accessible.name: qsTr("Styles")
                    }
                }
            }
        }
    }

    Dialogs.FolderDialog {
        id: folderDialog
        title: qsTr("Choose save folder")
        onAccepted: folderText.editText = findReplace.app.localPath(selectedFolder)
    }

    // Legacy Stylelistbox: OK lists the checked styles; Cancel empties the
    // field (GetCheckedElements returns nothing, kept legacy quirk).
    Dialog {
        id: stylesDialog
        objectName: "findStylesDialog"
        title: qsTr("Choose styles")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var names: []
        property var checkedNames: []
        function openWith(list) {
            names = list
            checkedNames = []
            open()
        }
        ColumnLayout {
            Repeater {
                model: stylesDialog.names
                CheckBox {
                    text: modelData
                    onToggled: {
                        const list = stylesDialog.checkedNames.filter(n => n !== modelData)
                        if (checked)
                            list.push(modelData)
                        stylesDialog.checkedNames = list
                    }
                }
            }
        }
        onAccepted: styles.text = names.filter(n => checkedNames.indexOf(n) >= 0).join(",")
        onRejected: styles.text = ""
    }

    // FindReplaceResultsDialog "Search results".
    Dialog {
        id: results
        objectName: "findResultsDialog"
        title: qsTr("Search results")
        modal: false
        width: Math.min(800, parent ? parent.width : 800)
        height: Math.min(560, parent ? parent.height : 560)
        anchors.centerIn: parent
        property bool replaceSet: false
        function openResults() {
            if (!replaceSet) {
                resultsReplace.editText = findReplace.replacements.length > 0 ? findReplace.replacements[0] : ""
                replaceSet = true
            }
            findReplace.rows = findReplace.app.findResults()
            open()
        }
        ColumnLayout {
            anchors.fill: parent
            ListView {
                id: resultsList
                objectName: "findResultsList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: findReplace.rows
                delegate: RowLayout {
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: resultsList.width
                    visible: modelData.header || modelData.visible
                    height: visible ? implicitHeight : 0
                    CheckBox {
                        checked: resultRow.modelData.checked
                        onClicked: findReplace.app.toggleFindResult(resultRow.index)
                    }
                    Label {
                        visible: resultRow.modelData.header
                        text: resultRow.modelData.text
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        TapHandler { onTapped: findReplace.app.toggleFindGroup(resultRow.index) }
                    }
                    Row {
                        visible: !resultRow.modelData.header
                        Layout.fillWidth: true
                        clip: true
                        Label { text: (resultRow.modelData.line || "") + (resultRow.modelData.before || "") }
                        Label {
                            id: matchLabel
                            text: resultRow.modelData.match || ""
                            background: Rectangle { color: matchLabel.palette.highlight }
                            color: matchLabel.palette.highlightedText
                        }
                        Label { text: resultRow.modelData.after || "" }
                        TapHandler { onDoubleTapped: findReplace.app.showFindResult(resultRow.index) }
                    }
                }
            }
            RowLayout {
                Button { text: qsTr("Check all"); onClicked: findReplace.app.checkFindResults(true) }
                Button { text: qsTr("Uncheck all"); onClicked: findReplace.app.checkFindResults(false) }
                Button {
                    id: replaceChecked
                    objectName: "replaceCheckedButton"
                    text: qsTr("Replace")
                    onClicked: findReplace.app.replaceFindResults(resultsReplace.editText)
                }
                ComboBox {
                    id: resultsReplace
                    objectName: "findResultsReplace"
                    editable: true
                    model: findReplace.replacements
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Replace with:")
                }
            }
        }
    }

    Connections {
        target: findReplace.app
        function onFindResultsChanged() {
            if (results.visible)
                findReplace.rows = findReplace.app.findResults()
            replaceChecked.enabled = findReplace.app.canReplaceFindResults()
        }
        function onFindQuestion(kind, text, title) { question.ask(kind, text, title) }
    }

    // HikariMessageBox / HikariMessageDialog: the application waits for the answer.
    Dialog {
        id: question
        objectName: "findQuestion"
        modal: true
        anchors.centerIn: parent
        property int kind: 0
        property alias text: questionText.text
        property bool answered: false
        function ask(k, t, caption) {
            kind = k
            text = t
            title = caption.length > 0 ? caption : qsTr("Find and Replace")
            answered = false
            open()
        }
        function answer(code) {
            answered = true
            close()
            findReplace.app.answerFindQuestion(code)
        }
        Label { id: questionText; objectName: "findQuestionText" }
        // 0 Ok, 1 Yes, 2 No, 3 Cancel.
        footer: DialogButtonBox {
            Button {
                objectName: "findAnswerOk"
                text: question.kind === 2 ? qsTr("Remove nonexistent styles") : qsTr("OK")
                visible: question.kind === 0 || question.kind === 2
                onClicked: question.answer(0)
            }
            Button {
                objectName: "findAnswerYes"
                text: question.kind === 2 || question.kind === 3 ? qsTr("Remove styles") : qsTr("Yes")
                visible: question.kind !== 0
                onClicked: question.answer(1)
            }
            Button {
                objectName: "findAnswerNo"
                text: question.kind === 2 ? qsTr("Ignore") : qsTr("No")
                visible: question.kind === 1 || question.kind === 2 || question.kind === 4
                onClicked: question.answer(2)
            }
            Button {
                objectName: "findAnswerCancel"
                text: qsTr("Cancel")
                visible: question.kind === 2 || question.kind === 3
                onClicked: question.answer(3)
            }
        }
        onClosed: if (!answered) answer(kind === 0 ? 0 : kind === 2 || kind === 3 ? 3 : 2)
    }
}
