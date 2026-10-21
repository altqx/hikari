// TM1 (#232): the Original text panel's toolbar, shown in its dock's header
// (Docking.setPanelHeader; MuseScore's toolbar slot,
// docs/research/musescore-shell.md §7). Legacy EditBox's translation-mode
// buttons (EDITBOX_PASTE_ALL_TO_TRANSLATION,
// EDITBOX_PASTE_SELECTION_TO_TRANSLATION, and EDITBOX_HIDE_ORIGINAL as
// "Comment out original") and its "Not confirmed" and "Moving tags" toggles
// (EditBox.cpp:248-258), icon-only with their names on hover. They take no
// focus, as legacy's, so the text keeps it. A header narrower than the
// buttons shows one "More" button whose menu holds them in the same order.
// Until the header takes it, it stays out of sight.
pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: tools
    required property var app
    required property var editor
    // Main's root: hotkeyGesture (Shift+click maps) and mappedTip.
    required property var shellRoot
    // The Original and Translated text fields ("Paste the selected").
    required property Item original
    required property Item translated
    visible: parent !== null && parent.objectName === "dockToolbarSlot"
    implicitWidth: buttons.implicitWidth
    implicitHeight: buttons.implicitHeight
    readonly property bool folded: width + 0.5 < buttons.implicitWidth

    function pasteAll() {
        if (!shellRoot.hotkeyGesture("EDITBOX_PASTE_ALL_TO_TRANSLATION", 2, true))
            editor.pasteAllToTranslation()
    }
    function pasteSelection() {
        if (!shellRoot.hotkeyGesture("EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2, true))
            editor.pasteSelectionToTranslation(original.selectionStart, original.selectionEnd, translated.cursorPosition)
    }
    function commentOut() {
        if (!shellRoot.hotkeyGesture("EDITBOX_HIDE_ORIGINAL", 2, true))
            editor.commentOutOriginal()
    }

    RowLayout {
        id: buttons
        objectName: "originalTextButtons"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: !tools.folded
        spacing: 2
        IconToolButton {
            objectName: "originalPasteAll"
            iconRole: "paste-translation"
            text: qsTranslate("HikariSub.Editor", "Paste all")
            tip: tools.shellRoot.mappedTip(text, "EDITBOX_PASTE_ALL_TO_TRANSLATION", 2)
            focusPolicy: Qt.NoFocus
            enabled: tools.editor.editable
            onClicked: tools.pasteAll()
        }
        IconToolButton {
            objectName: "originalPasteSelection"
            iconRole: "paste-selection-translation"
            text: qsTranslate("HikariSub.Editor", "Paste the selected")
            tip: tools.shellRoot.mappedTip(text, "EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2)
            focusPolicy: Qt.NoFocus
            enabled: tools.editor.editable
            onClicked: tools.pasteSelection()
        }
        IconToolButton {
            objectName: "originalCommentOut"
            iconRole: "comment-original"
            text: qsTranslate("HikariSub.Editor", "Comment out original")
            tip: tools.shellRoot.mappedTip(text, "EDITBOX_HIDE_ORIGINAL", 2)
            focusPolicy: Qt.NoFocus
            enabled: tools.editor.editable
            onClicked: tools.commentOut()
        }
        ToolSeparator {
            Layout.preferredHeight: 18
        }
        IconToolButton {
            objectName: "originalNotConfirmed"
            iconRole: "unconfirmed"
            text: qsTr("Not confirmed")
            tip: qsTr("Not confirmed: marks the selected Lines for review")
            checkable: true
            focusPolicy: Qt.NoFocus
            enabled: tools.editor.editable
            // SetLine: DoubtfulTL->SetValue(line->IsDoubtful()).
            checked: tools.editor.unconfirmed
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: {
                tools.editor.toggleUnconfirmed() // OnDoubtfulTl: the selected Lines
                checked = Qt.binding(() => tools.editor.unconfirmed)
            }
        }
        IconToolButton {
            objectName: "originalMovingTags"
            iconRole: "moving-tags"
            text: qsTr("Moving tags")
            checkable: true
            focusPolicy: Qt.NoFocus
            checked: tools.editor.moveTags
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: {
                tools.app.setMoveTags(checked) // OnAutoMoveTags: the option, saved at once
                checked = Qt.binding(() => tools.editor.moveTags)
            }
        }
    }

    IconToolButton {
        id: more
        objectName: "originalTextMore"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: tools.folded
        iconRole: "menu-more"
        text: qsTr("More")
        focusPolicy: Qt.NoFocus
        onClicked: moreMenu.popup(more, 0, more.height)
    }
    ShellMenu {
        id: moreMenu
        objectName: "originalTextMoreMenu"
        ShellMenuItem {
            objectName: "originalPasteAllItem"
            iconRole: "paste-translation"
            text: qsTranslate("HikariSub.Editor", "Paste all")
            enabled: tools.editor.editable
            onTriggered: tools.pasteAll()
        }
        ShellMenuItem {
            objectName: "originalPasteSelectionItem"
            iconRole: "paste-selection-translation"
            text: qsTranslate("HikariSub.Editor", "Paste the selected")
            enabled: tools.editor.editable
            onTriggered: tools.pasteSelection()
        }
        ShellMenuItem {
            objectName: "originalCommentOutItem"
            iconRole: "comment-original"
            text: qsTranslate("HikariSub.Editor", "Comment out original")
            enabled: tools.editor.editable
            onTriggered: tools.commentOut()
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "originalNotConfirmedItem"
            iconRole: "unconfirmed"
            text: qsTr("Not confirmed")
            checkable: true
            checked: tools.editor.unconfirmed
            enabled: tools.editor.editable
            onTriggered: {
                tools.editor.toggleUnconfirmed()
                checked = Qt.binding(() => tools.editor.unconfirmed)
            }
        }
        ShellMenuItem {
            objectName: "originalMovingTagsItem"
            iconRole: "moving-tags"
            text: qsTr("Moving tags")
            checkable: true
            checked: tools.editor.moveTags
            onTriggered: {
                tools.app.setMoveTags(checked)
                checked = Qt.binding(() => tools.editor.moveTags)
            }
        }
    }
}
