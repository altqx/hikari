pragma Translator: "HikariSub.InlineAi"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var controller
    required property bool ready
    property bool editor: false
    property int lineCount: 1
    signal chosen(var ask)
    signal settingsRequested()
    signal repeatRequested()
    title: qsTr("AI")
    iconRole: "ai-assistant"
    shown: !!controller && controller.on
    readonly property var customActions: controller ? controller.actions : []
    readonly property var lastAsk: controller ? controller.last : ({})

    ShellMenuItem {
        objectName: "inlineAiRewrite"
        iconRole: "edit"
        text: qsTr("Rewrite…")
        detailText: menu.editor ? "" : qsTr("%n line(s)", "", menu.lineCount)
        enabled: menu.ready
        onTriggered: menu.chosen({action: "rewrite"})
    }
    ShellMenuItem {
        objectName: "inlineAiShorten"
        iconRole: "compact"
        text: qsTr("Shorten to %1 CPS").arg(menu.controller ? menu.controller.maxCps : 15)
        help: qsTr("Rewrites each line over %1 characters per second so it reads at that speed or less").arg(menu.controller ? menu.controller.maxCps : 15)
        enabled: menu.ready
        onTriggered: menu.chosen({action: "shorten"})
    }
    ShellMenuItem {
        objectName: "inlineAiSpelling"
        iconRole: "dictionary"
        text: qsTr("Fix spelling and grammar")
        enabled: menu.ready
        onTriggered: menu.chosen({action: "spelling"})
    }
    ShellMenuItem {
        objectName: "inlineAiTranslate"
        iconRole: "language"
        text: qsTr("Translate to %1").arg(menu.controller ? menu.controller.translateTo : "")
        enabled: menu.ready
        onTriggered: menu.chosen({action: "translate", language: menu.controller.translateTo})
    }
    ShellMenuItem {
        objectName: "inlineAiTranslateTo"
        iconRole: "language"
        text: qsTr("Translate to…")
        enabled: menu.ready
        onTriggered: menu.chosen({action: "translate", choose: true})
    }
    Instantiator {
        model: menu.customActions
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "inlineAiCustom" + index
            iconRole: "ai-assistant"
            text: modelData.name
            help: modelData.instruction || ""
            enabled: menu.ready
            onTriggered: menu.chosen({action: "custom", custom: index, askEach: !!modelData.ask,
                                      instruction: modelData.ask ? "" : (modelData.instruction || "")})
        }
        onObjectAdded: (index, object) => menu.insertItem(menu.customStart + index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    MenuSeparator {}
    readonly property int customStart: 6
    ShellMenuItem {
        objectName: "inlineAiRepeat"
        command: "GLOBAL_REPEAT_AI_ACTION"
        iconRole: "repeat-run"
        text: qsTranslate("HikariSub.Hotkeys", "Repeat last AI action")
        detailText: MenuKeys.text(command, commandWindow) ? "" : (menu.lastAsk.label || "")
        enabled: menu.ready && !!menu.lastAsk.action
        onTriggered: menu.repeatRequested()
    }
    ShellMenuItem {
        objectName: "inlineAiSettings"
        iconRole: "settings"
        text: menu.ready ? qsTr("Edit actions…") : qsTr("Add a model in Settings › AI…")
        onTriggered: menu.settingsRequested()
    }
}
