pragma Translator: "HikariSub.InlineAi"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: box
    objectName: "inlineAiBox"
    required property var controller
    property var ask: ({})
    property bool editor: false
    property int spanStart: -1
    property int spanEnd: -1
    property string fieldText: ""
    property var plan: ({})
    property bool confirming: false
    readonly property bool translating: ask.action === "translate"
    readonly property bool typed: ask.action === "rewrite" || ask.action === "custom"
    readonly property string instruction: instructionField.text.trim()
    readonly property string language: languageBox.editText.trim()
    readonly property var languages: ["English", "Japanese", "Korean", "Chinese (Simplified)", "Chinese (Traditional)",
        "Thai", "Indonesian", "Vietnamese", "Spanish", "Portuguese (Brazil)", "French", "German", "Italian",
        "Polish", "Russian", "Ukrainian", "Turkish", "Arabic", "Tamil", "Hindi"]

    function askNow() {
        const a = Object.assign({}, ask)
        if (typed)
            a.instruction = instruction
        if (translating)
            a.language = language
        delete a.choose
        delete a.askEach
        delete a.repeat
        return a
    }
    function begin(a, isEditor, field) {
        ask = a
        editor = isEditor
        spanStart = field && field.selectionEnd > field.selectionStart ? field.selectionStart : -1
        spanEnd = field && field.selectionEnd > field.selectionStart ? field.selectionEnd : -1
        fieldText = field ? field.text : ""
        confirming = false
        instructionField.text = a.instruction || ""
        languageBox.editText = a.language || controller.translateTo
        const asks = !a.repeat && (a.action === "rewrite" || (a.action === "custom" && a.askEach)
                                   || (a.action === "translate" && a.choose))
        if (asks) {
            refresh()
            open()
            if (translating)
                languageBox.forceActiveFocus()
            else
                instructionField.forceActiveFocus()
            return
        }
        refresh()
        if (plan.confirm) {
            confirming = true
            open()
            return
        }
        controller.start(askNow(), editor, spanStart, spanEnd, fieldText)
    }
    function refresh() {
        plan = controller.plan(askNow(), editor)
    }
    function run() {
        if ((typed && instruction.length === 0) || (translating && language.length === 0) || plan.send === 0)
            return
        if (plan.confirm && !confirming) {
            confirming = true
            return
        }
        controller.start(askNow(), editor, spanStart, spanEnd, fieldText)
        close()
    }
    function money(dollars) {
        return "$" + Number(dollars).toLocaleString(Qt.locale(), "f", dollars < 0.1 ? 3 : 2)
    }
    readonly property string roleName: ({chat: qsTr("Chat"), translator: qsTr("Translator"),
                                         reviewer: qsTr("Reviewer")})[plan.role] || ""
    readonly property string verb: ask.action === "rewrite" ? qsTr("Rewrite")
                                  : ask.action === "translate" ? qsTr("Translate")
                                  : ask.action === "shorten" ? qsTr("Shorten")
                                  : ask.action === "spelling" ? qsTr("Fix spelling")
                                  : ((controller && controller.actions[ask.custom]) || {}).name || ""

    anchors.centerIn: parent
    width: Math.min(480, parent ? parent.width - 24 : 480)
    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.panel
        border.color: Theme.line
        radius: 4
    }
    Accessible.role: Accessible.Dialog
    Accessible.name: title.text

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.bottomMargin: 4
            spacing: 8
            Icon { iconRole: box.translating ? "language" : box.ask.action === "rewrite" ? "edit" : "ai-assistant"; size: 16 }
            Label {
                id: title
                objectName: "inlineAiBoxTitle"
                Layout.fillWidth: true
                font.bold: true
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: box.confirming ? qsTr("%1 %n line(s)", "", box.plan.send || 0).arg(box.verb)
                                     : box.editor ? box.verb : qsTr("%1 %n line(s)", "", box.plan.send || 0).arg(box.verb)
            }
            IconToolButton {
                iconRole: "window-close"
                text: qsTr("Close")
                onClicked: box.close()
            }
        }

        ColumnLayout {
            visible: !box.confirming
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 6
            ScrollView {
                visible: box.typed
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                TextArea {
                    id: instructionField
                    objectName: "inlineAiInstruction"
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("How to rewrite, for example: less formal, fewer words")
                    Accessible.name: qsTr("Instruction")
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier)
                            event.accepted = false
                        else
                            box.run()
                    }
                    Keys.onEnterPressed: box.run()
                }
            }
            Flow {
                objectName: "inlineAiRecent"
                visible: box.ask.action === "rewrite" && box.controller.recentInstructions.length > 0
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: box.controller ? box.controller.recentInstructions : []
                    delegate: Button {
                        required property string modelData
                        flat: true
                        text: modelData.length > 40 ? modelData.slice(0, 39) + "…" : modelData
                        ToolTip.visible: hovered && modelData.length > 40
                        ToolTip.text: modelData
                        Accessible.name: modelData
                        background: Rectangle {
                            radius: height / 2
                            color: parent.hovered ? Theme.select : "transparent"
                            border.color: Theme.line
                        }
                        onClicked: {
                            instructionField.text = modelData
                            instructionField.forceActiveFocus()
                        }
                    }
                }
            }
            ComboBox {
                id: languageBox
                objectName: "inlineAiLanguage"
                visible: box.translating
                editable: true
                model: box.languages
                Accessible.name: qsTr("Language")
                Layout.fillWidth: true
                onAccepted: box.run()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Icon { iconRole: "ai-assistant"; size: 14 }
                Label {
                    objectName: "inlineAiModel"
                    color: Theme.muted
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    text: box.plan.ready ? box.roleName + " · " + box.plan.model : qsTr("No model for this role")
                }
                Label {
                    objectName: "inlineAiEstimate"
                    color: Theme.muted
                    text: box.plan.estimate >= 0 ? qsTr("%n request(s) · about %1", "", box.plan.send || 0).arg(box.money(box.plan.estimate))
                                                 : qsTr("%n request(s)", "", box.plan.send || 0)
                }
            }
            Label {
                objectName: "inlineAiSkipped"
                visible: (box.plan.skipped || 0) > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("%n line(s) aren't sent: comments, empty lines, tag-only lines, drawings and karaoke.", "", box.plan.skipped || 0)
            }
        }

        ColumnLayout {
            visible: box.confirming
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 8
            Label {
                objectName: "inlineAiConfirmText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: qsTr("This sends %n request(s) to %1 with the %2 model, %3.", "", box.plan.send || 0)
                          .arg(box.plan.provider || "").arg(box.roleName).arg(box.plan.model || "")
            }
            Label {
                objectName: "inlineAiConfirmCost"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: (box.plan.estimate >= 0 ? qsTr("About %1.").arg(box.money(box.plan.estimate)) : qsTr("The model has no price, so the cost is unknown."))
                      + ((box.plan.dailyLeft ?? -1) >= 0 ? " " + qsTr("%1 left of today's %2 cap.").arg(box.money(box.plan.dailyLeft)).arg(box.money(box.plan.dailyCap)) : "")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("The run stops if it reaches the daily cap. Stop also works at any time.")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 8
            Label {
                objectName: "inlineAiMode"
                Layout.fillWidth: true
                color: Theme.muted
                elide: Text.ElideRight
                text: box.plan.mode === "manual" ? qsTr("Results wait for you to apply them: Manual")
                    : box.plan.mode === "full" ? qsTr("Changes go into the review: Full access")
                    : qsTr("Changes go into the review: Auto")
            }
            Button {
                objectName: "inlineAiCancel"
                text: qsTr("Cancel")
                onClicked: box.close()
            }
            PrimaryButton {
                objectName: "inlineAiRun"
                text: box.confirming ? qsTr("%1 %n line(s)", "", box.plan.send || 0).arg(box.verb) : box.verb
                enabled: (box.plan.send || 0) > 0 && box.plan.ready
                         && (!box.typed || box.instruction.length > 0) && (!box.translating || box.language.length > 0)
                onClicked: box.run()
            }
        }
    }
    onLanguageChanged: if (opened) refresh()
}
