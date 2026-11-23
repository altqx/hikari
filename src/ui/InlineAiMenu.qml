pragma Translator: "HikariSub.InlineAi"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var controller
    readonly property var ready: controller ? controller.ready : ({})
    property bool editor: false
    property int lineCount: 1
    property var field: null
    property var wordAt: ({start: -1, end: -1, text: ""})
    property string synonymWord: ""
    readonly property var synonyms: controller ? controller.synonyms : ({})
    signal chosen(var ask)
    signal settingsRequested()
    signal repeatRequested()
    title: qsTr("AI actions")
    iconRole: "ai-assistant"
    shown: !!controller && controller.on
    readonly property var customActions: controller ? controller.actions : []
    readonly property var lastAsk: controller ? controller.last : ({})
    onAboutToShow: {
        wordAt = field ? {start: field.selectionStart, end: field.selectionEnd, text: field.text} : {start: -1, end: -1, text: ""}
        synonymWord = editor && field && controller ? controller.synonymWord(wordAt.start, wordAt.end, wordAt.text) : ""
    }
    onClosed: if (controller) controller.cancelSynonyms()

    ShellMenuItem {
        objectName: "inlineAiRewrite"
        iconRole: "edit"
        text: qsTr("Rewrite…")
        detailText: menu.editor ? "" : qsTr("%n line(s)", "", menu.lineCount)
        enabled: !!menu.ready.chat
        onTriggered: menu.chosen({action: "rewrite"})
    }
    ShellMenuItem {
        objectName: "inlineAiShorten"
        iconRole: "compact"
        text: qsTr("Shorten to %1 CPS").arg(menu.controller ? menu.controller.maxCps : 15)
        help: qsTr("Rewrites each line over %1 characters per second so it reads at that speed or less").arg(menu.controller ? menu.controller.maxCps : 15)
        enabled: !!menu.ready.chat
        onTriggered: menu.chosen({action: "shorten"})
    }
    ShellMenuItem {
        objectName: "inlineAiSpelling"
        iconRole: "dictionary"
        text: qsTr("Fix spelling and grammar")
        enabled: !!menu.ready.reviewer
        onTriggered: menu.chosen({action: "spelling"})
    }
    ShellMenu {
        id: synonymsMenu
        objectName: "inlineAiSynonyms"
        iconRole: "dictionary"
        title: qsTr("Suggest synonyms")
        shown: menu.synonymWord !== ""
        enabled: !!menu.ready.chat
        onAboutToShow: menu.controller.suggestSynonyms(menu.wordAt.start, menu.wordAt.end, menu.wordAt.text)
        ShellMenuItem {
            objectName: "inlineAiSynonymsState"
            readonly property int stateIcon: menu.synonyms.state === "none" ? 1
                : menu.synonyms.state === "failed" || menu.synonyms.state === "dailyCap" ? 2 : 0
            iconRole: ["run-busy", "notice-info", "notice-warning"][stateIcon]
            enabled: false
            visible: menu.synonyms.state !== "found"
            height: visible ? implicitHeight : 0
            text: menu.synonyms.state === "none" ? qsTr("No synonyms for “%1”").arg(menu.synonyms.word)
                : menu.synonyms.state === "failed" ? qsTr("Couldn’t suggest synonyms: %1").arg(menu.synonyms.error)
                : menu.synonyms.state === "dailyCap" ? qsTr("Today’s cost cap is reached.")
                : qsTr("Finding synonyms…")
        }
        Instantiator {
            model: menu.synonyms.words || []
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: "inlineAiSynonym" + index
                iconRole: "dictionary"
                text: modelData
                onTriggered: menu.controller.useSynonym(modelData)
            }
            onObjectAdded: (index, object) => synonymsMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => synonymsMenu.removeItem(object)
        }
    }
    ShellMenuItem {
        objectName: "inlineAiTranslate"
        iconRole: "language"
        text: qsTr("Translate to %1").arg(menu.controller ? menu.controller.translateTo : "")
        enabled: !!menu.ready.translator
        onTriggered: menu.chosen({action: "translate", language: menu.controller.translateTo})
    }
    ShellMenuItem {
        objectName: "inlineAiTranslateTo"
        iconRole: "language"
        text: qsTr("Translate to…")
        enabled: !!menu.ready.translator
        onTriggered: menu.chosen({action: "translate", choose: true})
    }
    MenuSeparator {
        id: customSeparator
        objectName: "inlineAiCustomSeparator"
        visible: menu.customActions.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: menu.customActions
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "inlineAiCustom" + index
            iconRole: "ai-assistant"
            text: modelData.ask ? qsTr("%1…").arg(modelData.name) : modelData.name
            help: modelData.instruction || ""
            enabled: !!menu.ready[modelData.role || "chat"]
            onTriggered: menu.chosen({action: "custom", custom: index, askEach: !!modelData.ask,
                                      instruction: modelData.ask ? "" : (modelData.instruction || "")})
        }
        onObjectAdded: (index, object) => menu.insertItem(menu.customStart() + index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    function customStart() {
        for (let i = 0; i < count; ++i) {
            if (itemAt(i) === customSeparator)
                return i + 1
        }
        return count
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "inlineAiRepeat"
        command: "GLOBAL_REPEAT_AI_ACTION"
        iconRole: "repeat-run"
        text: qsTranslate("HikariSub.Hotkeys", "Repeat last AI action")
        detailText: MenuKeys.text(command, commandWindow) ? "" : (menu.lastAsk.label || "")
        enabled: !!menu.ready.any && !!menu.lastAsk.action
        onTriggered: menu.repeatRequested()
    }
    ShellMenuItem {
        objectName: "inlineAiSettings"
        iconRole: "settings"
        text: menu.ready.any ? qsTr("Edit actions") : qsTr("Add a model in Settings › AI")
        onTriggered: menu.settingsRequested()
    }
}
