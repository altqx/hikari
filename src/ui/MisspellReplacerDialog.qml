// F4: legacy MisspellReplacer "Multireplacer" (GLOBAL_MISSPELLS_REPLACER,
// "Fix minor errors (experimental)"), a modeless dialog: rule editing, the
// rules list with its checkboxes, "Which lines", and finding or replacing
// with the checked rules. Finds open the FindResultDialog "Search results",
// where the checked ones are replaced.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "misspellDialog"
    required property var app
    title: qsTr("Multireplacer")
    modal: false
    // Modeless like the legacy window: a click elsewhere (the other dialog,
    // the editor, the grid) does not close it.
    closePolicy: Popup.CloseOnEscape
    property var rules: []
    property int selectedRule: -1

    function openDialog() {
        reloadRules()
        open()
    }
    // The menu shows or hides it (MR->Show(!MR->IsShown())).
    function toggle() {
        if (visible)
            close()
        else
            openDialog()
    }
    function reloadRules() {
        rules = app.misspellRules()
        if (selectedRule >= rules.length)
            selectedRule = -1
    }
    function options() {
        return (matchCase.checked ? 1 : 0) | (lowerCase.checked ? 2 : 0) | (upperCase.checked ? 4 : 0)
                | (unchangedCase.checked ? 8 : 0) | (onlyTags.checked ? 16 : 0) | (onlyText.checked ? 32 : 0)
    }
    function fields() {
        return {description: description.text, find: findPhrase.text, replace: replacePhrase.text, options: options()}
    }
    // LIST_ITEM_LEFT_CLICK: the rule's values go to the editing fields.
    function chooseRule(index) {
        selectedRule = index
        const r = rules[index]
        description.text = r.description
        findPhrase.text = r.find
        replacePhrase.text = r.replace
        matchCase.checked = (r.options & 1) !== 0
        lowerCase.checked = (r.options & 2) !== 0
        upperCase.checked = (r.options & 4) !== 0
        unchangedCase.checked = (r.options & 8) !== 0
        onlyTags.checked = (r.options & 16) !== 0
        onlyText.checked = (r.options & 32) !== 0
    }
    function scope() {
        return {lines: whichLines.currentIndex, styles: styles.text}
    }
    function find(allTabs) {
        results.show(app.findMisspells(scope(), allTabs))
    }

    RowLayout {
        anchors.fill: parent
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GroupBox {
                title: qsTr("Rule editing")
                Layout.fillWidth: true
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    Label { text: qsTr("Rule description"); Layout.columnSpan: 2 }
                    TextField {
                        id: description
                        objectName: "misspellDescription"
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Rule description")
                    }
                    Label { text: qsTr("Search phrase (regular expresions)"); Layout.fillWidth: true }
                    Label { text: qsTr("Replace phrase"); Layout.fillWidth: true }
                    TextField { id: findPhrase; objectName: "misspellFind"; Layout.fillWidth: true; Accessible.name: qsTr("Search phrase (regular expresions)") }
                    TextField { id: replacePhrase; objectName: "misspellReplace"; Layout.fillWidth: true; Accessible.name: qsTr("Replace phrase") }
                    CheckBox { id: matchCase; objectName: "misspellMatchCase"; text: qsTr("Match case") }
                    CheckBox { id: lowerCase; text: qsTr("Change to lower case") }
                    CheckBox { id: upperCase; text: qsTr("Change to upper case") }
                    CheckBox { id: unchangedCase; text: qsTr("Dont change case") }
                    CheckBox { id: onlyTags; text: qsTr("Replace only in tags") }
                    CheckBox { id: onlyText; text: qsTr("Replace only in text") }
                }
            }
            // The rules list: checkbox, Description, Rule find, Rule replace.
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 520
                Layout.minimumHeight: 260
                padding: 2
                ListView {
                    id: rulesList
                    objectName: "misspellRules"
                    anchors.fill: parent
                    clip: true
                    model: dialog.rules
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Rules")
                    header: RowLayout {
                        width: rulesList.width
                        Item { Layout.preferredWidth: 28 }
                        Label { text: qsTr("Description"); font.bold: true; Layout.preferredWidth: rulesList.width * 0.5 }
                        Label { text: qsTr("Rule find"); font.bold: true; Layout.fillWidth: true }
                        Label { text: qsTr("Rule replace"); font.bold: true; Layout.fillWidth: true }
                    }
                    delegate: Rectangle {
                        id: ruleRow
                        required property var modelData
                        required property int index
                        width: rulesList.width
                        height: ruleLayout.implicitHeight
                        color: index === dialog.selectedRule ? dialog.palette.highlight : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.description
                        MouseArea {
                            anchors.fill: parent
                            onClicked: dialog.chooseRule(ruleRow.index)
                        }
                        RowLayout {
                            id: ruleLayout
                            width: parent.width
                            CheckBox {
                                objectName: "misspellRuleCheck" + ruleRow.index
                                checked: ruleRow.modelData.checked
                                Accessible.name: ruleRow.modelData.description
                                onToggled: dialog.app.checkMisspellRule(ruleRow.index, checked)
                            }
                            Label { text: ruleRow.modelData.description; elide: Text.ElideRight; Layout.preferredWidth: rulesList.width * 0.5 }
                            Label { text: ruleRow.modelData.find; elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                            Label { text: ruleRow.modelData.replace; elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            GroupBox {
                title: qsTr("Which lines")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    ComboBox {
                        id: whichLines
                        objectName: "misspellWhichLines"
                        Layout.fillWidth: true
                        model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From the selected line"),
                                qsTr("According to the selected styles")]
                        Accessible.name: qsTr("Which lines")
                    }
                    RowLayout {
                        // Legacy binds nothing to this button.
                        Button { text: "+"; Accessible.name: qsTr("Choose styles") }
                        TextField { id: styles; objectName: "misspellStyles"; Layout.fillWidth: true; Accessible.name: qsTr("Styles") }
                    }
                }
            }
            Button {
                objectName: "misspellAddRule"
                text: qsTr("Add rule")
                Layout.fillWidth: true
                onClicked: if (dialog.app.addMisspellRule(dialog.fields())) dialog.reloadRules()
            }
            Button {
                objectName: "misspellEditRule"
                text: qsTr("Edit rule")
                Layout.fillWidth: true
                onClicked: if (dialog.app.editMisspellRule(dialog.selectedRule, dialog.fields())) dialog.reloadRules()
            }
            Button {
                objectName: "misspellRemoveRule"
                text: qsTr("Delete rule")
                Layout.fillWidth: true
                onClicked: if (dialog.app.removeMisspellRule(dialog.selectedRule)) dialog.reloadRules()
            }
            // "Find error" and "Replace error" are not implemented in legacy (disabled).
            Button { text: qsTr("Find error"); enabled: false; Layout.fillWidth: true }
            Button {
                objectName: "misspellFindTab"
                text: qsTr("Find errors\nin current tab")
                Layout.fillWidth: true
                onClicked: dialog.find(false)
            }
            Button {
                objectName: "misspellFindAllTabs"
                text: qsTr("Find errors\nin all tabs")
                Layout.fillWidth: true
                onClicked: dialog.find(true)
            }
            Button { text: qsTr("Replace error"); enabled: false; Layout.fillWidth: true }
            Button {
                objectName: "misspellReplaceTab"
                text: qsTr("Replace all errors\nin current tab")
                Layout.fillWidth: true
                onClicked: dialog.app.replaceMisspells(dialog.scope(), false)
            }
            Button {
                objectName: "misspellReplaceAllTabs"
                text: qsTr("Replace all errors\nin all tabs")
                Layout.fillWidth: true
                onClicked: dialog.app.replaceMisspells(dialog.scope(), true)
            }
        }
    }

    // FindResultDialog "Search results": a header per Document (its path)
    // and its finds as "Line %i: " and the text with the find marked. The
    // header's checkbox checks its finds, a click on its text folds them; a
    // double click on a find shows it. Replace works once per search.
    Dialog {
        id: results
        objectName: "misspellResults"
        parent: Overlay.overlay
        title: qsTr("Search results")
        modal: false
        closePolicy: Popup.CloseOnEscape
        // A separate window in legacy: placed at the lower left so the
        // Multireplacer's buttons stay reachable beside it.
        x: 8
        y: Math.max(8, parent.height - height - 8)
        property var rows: []
        property var checks: []
        property var folded: []
        property bool canReplace: true

        function show(list) {
            rows = list
            checks = list.map(() => true)
            folded = list.map(() => false)
            canReplace = true
            if (!visible)
                open()
        }
        function setChecks(next) { checks = next }
        // The header row of row `i`.
        function headerOf(i) {
            while (i >= 0 && !rows[i].header)
                --i
            return i
        }
        function toggleRow(i) {
            const next = checks.slice()
            next[i] = !next[i]
            if (rows[i].header) {
                for (let j = i + 1; j < rows.length && !rows[j].header; ++j)
                    next[j] = next[i]
            } else {
                const h = headerOf(i)
                let any = false
                for (let j = h + 1; j < rows.length && !rows[j].header; ++j)
                    any = any || next[j]
                if (h >= 0)
                    next[h] = any
            }
            checks = next
        }
        function toggleFold(i) {
            const next = folded.slice()
            next[i] = !next[i]
            folded = next
        }
        function replaceChecked() {
            const finds = []
            for (let i = 0; i < rows.length; ++i)
                if (!rows[i].header && checks[i])
                    finds.push(rows[i].find)
            dialog.app.replaceMisspellFinds(finds)
            canReplace = false
        }
        onVisibleChanged: dialog.app.setMisspellResultsShown(visible)

        ColumnLayout {
            anchors.fill: parent
            ListView {
                id: resultsList
                objectName: "misspellResultsList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 700
                Layout.minimumHeight: 300
                clip: true
                model: results.rows
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Search results")
                delegate: Item {
                    id: resultRow
                    required property var modelData
                    required property int index
                    readonly property bool hidden: !modelData.header && results.folded[results.headerOf(index)] === true
                    width: resultsList.width
                    height: hidden ? 0 : resultLayout.implicitHeight
                    visible: !hidden
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.header ? modelData.text : qsTr("Line %1: ").arg(modelData.line) + modelData.text
                    RowLayout {
                        id: resultLayout
                        width: parent.width
                        spacing: 0
                        CheckBox {
                            objectName: "misspellResultCheck" + resultRow.index
                            checked: results.checks[resultRow.index] === true
                            onToggled: results.toggleRow(resultRow.index)
                        }
                        Label {
                            objectName: "misspellResultHeader" + resultRow.index
                            visible: resultRow.modelData.header
                            text: resultRow.modelData.header ? resultRow.modelData.text : ""
                            font.bold: true
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            MouseArea {
                                anchors.fill: parent
                                onClicked: results.toggleFold(resultRow.index)
                            }
                        }
                        Label {
                            visible: !resultRow.modelData.header
                            text: resultRow.modelData.header ? ""
                                  : qsTr("Line %1: ").arg(resultRow.modelData.line)
                                    + resultRow.modelData.text.substring(0, resultRow.modelData.position)
                        }
                        Label {
                            visible: !resultRow.modelData.header
                            text: resultRow.modelData.header ? ""
                                  : resultRow.modelData.text.substr(resultRow.modelData.position, resultRow.modelData.length)
                            color: dialog.palette.highlightedText
                            background: Rectangle { color: dialog.palette.highlight }
                        }
                        Label {
                            visible: !resultRow.modelData.header
                            text: resultRow.modelData.header ? ""
                                  : resultRow.modelData.text.substring(resultRow.modelData.position + resultRow.modelData.length)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        objectName: "misspellResultRow" + resultRow.index
                        anchors.fill: parent
                        anchors.leftMargin: 40
                        enabled: !resultRow.modelData.header
                        onDoubleClicked: dialog.app.showMisspellFind(resultRow.modelData.find)
                    }
                }
            }
            RowLayout {
                Button {
                    objectName: "misspellCheckAll"
                    text: qsTr("Check all")
                    onClicked: results.setChecks(results.rows.map(() => true))
                }
                Button {
                    objectName: "misspellUncheckAll"
                    text: qsTr("Uncheck all")
                    onClicked: results.setChecks(results.rows.map(() => false))
                }
                Button {
                    objectName: "misspellReplaceChecked"
                    text: qsTr("Replace")
                    enabled: results.canReplace
                    onClicked: results.replaceChecked()
                }
            }
        }
    }
}
