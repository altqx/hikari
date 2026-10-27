// LE1 (#233): the Line editor's tag strip, one row of icon buttons in groups
// split by thin rules: the tag buttons (legacy BoxSizer4), the font and the
// four colours (E1), the translation buttons and toggles while the Original
// text panel is closed (TM1's OriginalTextTools, in the strip), Text
// position (E4, LE4) and the custom tag buttons (E2, LE5), with the
// counters (LineCounters) at the row's end. The buttons take no focus, so
// the text keeps it and its selection; each one's name and hotkey show on
// hover.
//
// LE1-strip-folds: groups that do not fit go, last first, into a More
// button's menu (icons, names and hotkeys; Text position as a submenu, the
// custom buttons after it). The tag buttons always show, so the strip
// needs only them, More and the counters: a long row of custom buttons
// never widens the Line editor's dock.
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: strip
    objectName: "tagStrip"
    // Main's root: editedField, mappedTip, boundKeys, hotkeyGesture,
    // colourClick, applyTagButton and focusPanel.
    required property var shellRoot
    required property LineEditorController editor
    required property var app
    required property TagButtonsController tagButtons
    required property var fontPicker
    // The Original and Translated text fields ("Paste the selected").
    required property Item original
    required property Item translated
    // The translation group shows while the Original text panel is closed.
    required property bool translationTools
    spacing: 2

    // The strip at its narrowest: the tag buttons, More and the counters.
    readonly property real minimumWidth: tagGroup.implicitWidth + 2 * spacing + more.implicitWidth + gap
                                         + counters.implicitWidth
    readonly property int gap: 8 // before the counters
    // The groups that fold, in order; the first `shownGroups` show.
    readonly property var foldable: [lookGroup, translationGroup, alignGroup, customGroup]
    // A group's width from its buttons, also while it is folded (hidden).
    function groupWidth(g) {
        let w = 0, n = 0
        for (const c of g.children) {
            // (a Repeater is 0 wide; a popup is no child item)
            if (c.implicitWidth <= 0 || c.stripHidden === true)
                continue
            w += c.implicitWidth
            ++n
        }
        return w + Math.max(0, n - 1) * g.spacing
    }
    readonly property int shownGroups: {
        const widths = foldable.map(g => g.active ? groupWidth(g) + spacing : 0)
        let room = width - tagGroup.implicitWidth - gap - counters.implicitWidth
        if (widths.reduce((a, b) => a + b, 0) <= room)
            return foldable.length
        room -= more.implicitWidth + spacing
        let n = 0
        while (n < foldable.length && widths[n] <= room)
            room -= widths[n++]
        return n
    }
    function isFolded(g) { return g.active && foldable.indexOf(g) >= shownGroups }
    // Where a colour's popover opens: its button, or More while it is folded.
    function colourAnchor(number) {
        return lookGroup.visible ? colourButtons.itemAt(number - 1) : more
    }
    function colourName(number) { return colourButtons.model[number - 1].name }

    component Group: RowLayout {
        property bool active: true
        required property int order // in `foldable`
        visible: active && order < strip.shownGroups
        spacing: 2
    }
    component FoldSeparator: MenuSeparator {
        height: visible ? implicitHeight : 0
    }
    component Rule: ToolSeparator {
        Layout.preferredHeight: 18
    }

    RowLayout {
        id: tagGroup
        objectName: "tagGroup"
        spacing: 2
        Repeater {
            // O2: mapped buttons (EDITBOX_INSERT_BOLD ...): Shift+click
            // maps the hotkey, the tooltip shows it.
            // K1: the set's icons in place of the letters
            // (legacy EditBox's BOLD, ITALIC, UNDER, STRIKE
            // bitmaps, EditBox.cpp:168-195).
            model: [
                { tag: "b", name: qsTr("Bold"), symbol: "EDITBOX_INSERT_BOLD" },
                { tag: "i", name: qsTr("Italic"), symbol: "EDITBOX_INSERT_ITALIC" },
                { tag: "u", name: qsTr("Underline"), symbol: "EDITBOX_CHANGE_UNDERLINE" },
                { tag: "s", name: qsTr("Strikeout"), symbol: "EDITBOX_CHANGE_STRIKEOUT" }
            ]
            IconToolButton {
                required property var modelData
                required property int index
                objectName: "tag_" + modelData.tag
                iconRole: ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout"][index]
                text: modelData.name
                focusPolicy: Qt.NoFocus
                enabled: strip.editor.editable
                tip: strip.shellRoot.mappedTip(modelData.name, modelData.symbol, 2)
                onClicked: {
                    if (strip.shellRoot.hotkeyGesture(modelData.symbol, 2, true))
                        return
                    const field = strip.shellRoot.editedField()
                    strip.editor.toggleTagIn(field.role, modelData.tag, field.selectionStart, field.selectionEnd)
                }
            }
        }
    }

    // E1: Font selection and the four colours (EDITBOX_CHANGE_FONT,
    // EDITBOX_CHANGE_COLOR_*).
    Group {
        id: lookGroup
        order: 0
        objectName: "lookGroup"
        Rule {}
        IconToolButton {
            objectName: "changeFont"
            iconRole: "tag-font"
            text: qsTr("Font selection")
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            tip: strip.shellRoot.mappedTip(text, "EDITBOX_CHANGE_FONT", 2)
            onClicked: if (!strip.shellRoot.hotkeyGesture("EDITBOX_CHANGE_FONT", 2, true)) strip.chooseFont()
        }
        Repeater {
            id: colourButtons
            model: [
                { number: 1, name: qsTr("Primary color"), symbol: "EDITBOX_CHANGE_COLOR_PRIMARY" },
                { number: 2, name: qsTr("Secondary color for karaoke"), symbol: "EDITBOX_CHANGE_COLOR_SECONDARY" },
                { number: 3, name: qsTr("Border color"), symbol: "EDITBOX_CHANGE_COLOR_OUTLINE" },
                { number: 4, name: qsTr("Shadow color"), symbol: "EDITBOX_CHANGE_COLOR_SHADOW" }
            ]
            IconToolButton {
                required property var modelData
                objectName: "changeColour" + modelData.number
                iconRole: ["colour-primary", "colour-secondary", "colour-outline", "colour-shadow"][modelData.number - 1]
                text: modelData.name
                focusPolicy: Qt.NoFocus
                enabled: strip.editor.editable
                tip: strip.shellRoot.mappedTip(modelData.name, modelData.symbol, 2)
                onClicked: {
                    if (strip.shellRoot.hotkeyGesture(modelData.symbol, 2, true))
                        return
                    strip.shellRoot.colourClick(modelData.number, true, strip.shellRoot.editedField())
                }
                // Y7: the right click (wxEVT_RIGHT_UP, EditBox.cpp:184-196).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: if (parent.enabled) strip.shellRoot.colourClick(parent.modelData.number, false,
                                                                            strip.shellRoot.editedField())
                }
            }
        }
    }
    function chooseFont(edited) {
        const field = edited ?? shellRoot.editedField()
        fontPicker.openFor(field.role, field.selectionStart, field.selectionEnd)
    }

    // TM1: legacy's translation-mode buttons and toggles (OriginalTextTools),
    // here while the Original text panel is closed (the panel's header holds
    // them while it shows).
    Group {
        id: translationGroup
        order: 1
        objectName: "translationButtons"
        active: strip.translationTools
        Rule {}
        IconToolButton {
            objectName: "pasteAllToTranslation"
            iconRole: "paste-translation"
            text: qsTr("Paste all")
            tip: strip.shellRoot.mappedTip(text, "EDITBOX_PASTE_ALL_TO_TRANSLATION", 2)
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            onClicked: strip.pasteAll()
        }
        IconToolButton {
            objectName: "pasteSelectionToTranslation"
            iconRole: "paste-selection-translation"
            text: qsTr("Paste the selected")
            tip: strip.shellRoot.mappedTip(text, "EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2)
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            onClicked: strip.pasteSelection()
        }
        IconToolButton {
            objectName: "commentOutOriginal"
            iconRole: "comment-original"
            text: qsTr("Comment out original")
            tip: strip.shellRoot.mappedTip(text, "EDITBOX_HIDE_ORIGINAL", 2)
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            onClicked: strip.commentOut()
        }
        Rule {}
        // E5: legacy DoubtfulTL and AutoMoveTags (EditBox.cpp:248-258).
        IconToolButton {
            objectName: "notConfirmed"
            iconRole: "unconfirmed"
            text: qsTranslate("HikariSub.Translation", "Not confirmed")
            tip: qsTranslate("HikariSub.Translation", "Not confirmed: marks the selected Lines for review")
            checkable: true
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            // SetLine: DoubtfulTL->SetValue(line->IsDoubtful()).
            checked: strip.editor.unconfirmed
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: {
                strip.editor.toggleUnconfirmed() // OnDoubtfulTl: the selected Lines
                checked = Qt.binding(() => strip.editor.unconfirmed)
            }
        }
        IconToolButton {
            objectName: "movingTags"
            iconRole: "moving-tags"
            text: qsTranslate("HikariSub.Translation", "Tags from original")
            tip: qsTranslate("HikariSub.Translation", "Tags from original: the translation gets the Original text's tags")
            checkable: true
            focusPolicy: Qt.NoFocus
            checked: strip.editor.moveTags
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: {
                strip.app.setMoveTags(checked) // OnAutoMoveTags: the option, saved at once
                checked = Qt.binding(() => strip.editor.moveTags)
            }
        }
    }
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

    // E4: Text position (legacy Ban, after the colours).
    Group {
        id: alignGroup
        order: 2
        objectName: "alignGroup"
        Rule {}
        AlignmentChoice {
            id: alignment
            editor: strip.editor
            // TM1: the Original text may be in its own panel, floating or a hidden tab.
            onChosen: strip.shellRoot.focusPanel(strip.editor.translationMode && strip.editor.translationText.length
                                                 ? strip.translated : strip.original, Qt.OtherFocusReason)
        }
    }

    // E2: custom tag buttons. LE5: a button without a tag opens its
    // popover; the right click its menu.
    Group {
        id: customGroup
        order: 3
        objectName: "customGroup"
        Rule {}
        Repeater {
            model: strip.tagButtons.buttons
            TagButton {
                id: tagButton
                required property var modelData
                required property int index
                objectName: "tagButton" + index
                text: modelData.name
                iconRole: modelData.icon
                focusPolicy: Qt.NoFocus
                enabled: strip.editor.editable
                // An icon-only button reads as its tag, as in Settings' list.
                Accessible.name: modelData.name.length > 0 ? modelData.name : modelData.tag
                Accessible.description: modelData.tag
                tip: strip.shellRoot.mappedTip(modelData.name.length > 0 && modelData.icon.length > 0
                                               ? modelData.name + ": " + modelData.tag : modelData.tag,
                                               "EDITBOX_TAG_BUTTON" + (index + 1), 2)
                onClicked: {
                    if (strip.shellRoot.hotkeyGesture("EDITBOX_TAG_BUTTON" + (index + 1), 2, true))
                        return
                    strip.runTagButton(tagButton, index, modelData)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => tagButtonMenu.openFor(tagButton, tagButton.index, eventPoint.position)
                }
            }
        }
        IconToolButton {
            id: addTagButton
            objectName: "addTagButton"
            readonly property bool stripHidden: strip.tagButtons.count >= strip.tagButtons.maximum
            iconRole: "add"
            text: qsTr("Add a tag button")
            focusPolicy: Qt.NoFocus
            visible: !stripHidden
            onClicked: tagButtonEditor.openFor(addTagButton, -1, null)
        }
        TagButtonEditor {
            id: tagButtonEditor
            tagButtons: strip.tagButtons
            onSaved: (index, b) => index < 0 ? strip.tagButtons.add(b.name, b.tag, b.type, b.icon)
                                             : strip.tagButtons.edit(index, b.name, b.tag, b.type, b.icon)
        }
        // LE5: a button's context menu.
        ShellMenu {
            id: tagButtonMenu
            objectName: "tagButtonMenu"
            property Item button: null
            property int index: -1
            // Left is toward the first button, unless the layout is mirrored.
            readonly property int leftStep: Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1
            // At the click, inside the button it is for.
            function openFor(b, i, at) {
                button = b
                index = i
                popup(b, at)
            }
            function canMove(step) {
                return index >= 0 && index + step >= 0 && index + step < strip.tagButtons.count
            }
            ShellMenuItem {
                objectName: "tagButtonEdit"
                iconRole: "edit"
                text: qsTr("Edit...")
                onTriggered: tagButtonEditor.openFor(tagButtonMenu.button, tagButtonMenu.index,
                                                     strip.tagButtons.buttons[tagButtonMenu.index])
            }
            ShellMenuItem {
                objectName: "tagButtonMoveLeft"
                iconRole: "move-left"
                text: qsTr("Move left")
                enabled: tagButtonMenu.canMove(tagButtonMenu.leftStep)
                onTriggered: strip.tagButtons.move(tagButtonMenu.index, tagButtonMenu.leftStep)
            }
            ShellMenuItem {
                objectName: "tagButtonMoveRight"
                iconRole: "move-right"
                text: qsTr("Move right")
                enabled: tagButtonMenu.canMove(-tagButtonMenu.leftStep)
                onTriggered: strip.tagButtons.move(tagButtonMenu.index, -tagButtonMenu.leftStep)
            }
            MenuSeparator {}
            ShellMenuItem {
                objectName: "tagButtonRemove"
                iconRole: "remove"
                text: qsTr("Remove")
                onTriggered: strip.tagButtons.remove(tagButtonMenu.index)
            }
        }
        // LE3-tags-shown: the hidden-tag view's switch left the strip for
        // Settings > Editor and GLOBAL_EDITOR_HIDE_TAGS (Subtitles > "Hide
        // tags in the line editor").
    }
    // A custom button's click (or its item in More): its tag, or the
    // popover that gives it one.
    function runTagButton(anchor, index, b, edited) {
        if (b.tag.length === 0)
            tagButtonEditor.openFor(anchor, index, b)
        else
            shellRoot.applyTagButton(index, edited)
    }

    IconToolButton {
        id: more
        objectName: "tagStripMore"
        iconRole: "menu-more"
        text: qsTranslate("HikariSub.Translation", "More")
        focusPolicy: Qt.NoFocus
        visible: strip.foldable.some(g => strip.isFolded(g))
        onClicked: {
            // The menu takes the focus: its items act on the field edited now.
            moreMenu.field = strip.shellRoot.editedField()
            moreMenu.popup(more, 0, more.height)
        }
    }
    Item {
        Layout.fillWidth: true
        Layout.minimumWidth: strip.gap
    }
    LineCounters {
        id: counters
        editor: strip.editor
    }

    // The folded groups' buttons, in the strip's order.
    ShellMenu {
        id: moreMenu
        objectName: "tagStripMoreMenu"
        property Item field
        readonly property bool look: strip.isFolded(lookGroup)
        readonly property bool translation: strip.isFolded(translationGroup)
        readonly property bool align: strip.isFolded(alignGroup)
        readonly property bool custom: strip.isFolded(customGroup)
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedFont"
            visible: moreMenu.look
            iconRole: "tag-font"
            text: qsTr("Font selection")
            shortcutText: strip.shellRoot.boundKeys("EDITBOX_CHANGE_FONT", 2)
            enabled: strip.editor.editable
            onTriggered: strip.chooseFont(moreMenu.field)
        }
        Instantiator {
            model: moreMenu.look ? colourButtons.model : []
            delegate: ShellMenuItem {
                height: visible ? implicitHeight : 0
                required property var modelData
                objectName: "foldedColour" + modelData.number
                iconRole: ["colour-primary", "colour-secondary", "colour-outline", "colour-shadow"][modelData.number - 1]
                text: modelData.name
                shortcutText: strip.shellRoot.boundKeys(modelData.symbol, 2)
                enabled: strip.editor.editable
                onTriggered: strip.shellRoot.colourClick(modelData.number, true, moreMenu.field ?? strip.shellRoot.editedField())
            }
            onObjectAdded: (index, object) => moreMenu.insertItem(1 + index, object)
            onObjectRemoved: (index, object) => moreMenu.removeItem(object)
        }
        FoldSeparator { visible: moreMenu.look && (moreMenu.translation || moreMenu.align || moreMenu.custom) }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedPasteAll"
            visible: moreMenu.translation
            iconRole: "paste-translation"
            text: qsTr("Paste all")
            shortcutText: strip.shellRoot.boundKeys("EDITBOX_PASTE_ALL_TO_TRANSLATION", 2)
            enabled: strip.editor.editable
            onTriggered: strip.editor.pasteAllToTranslation()
        }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedPasteSelection"
            visible: moreMenu.translation
            iconRole: "paste-selection-translation"
            text: qsTr("Paste the selected")
            shortcutText: strip.shellRoot.boundKeys("EDITBOX_PASTE_SELECTION_TO_TRANSLATION", 2)
            enabled: strip.editor.editable
            onTriggered: strip.editor.pasteSelectionToTranslation(strip.original.selectionStart, strip.original.selectionEnd,
                                                                  strip.translated.cursorPosition)
        }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedCommentOut"
            visible: moreMenu.translation
            iconRole: "comment-original"
            text: qsTr("Comment out original")
            shortcutText: strip.shellRoot.boundKeys("EDITBOX_HIDE_ORIGINAL", 2)
            enabled: strip.editor.editable
            onTriggered: strip.editor.commentOutOriginal()
        }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedNotConfirmed"
            visible: moreMenu.translation
            iconRole: "unconfirmed"
            text: qsTranslate("HikariSub.Translation", "Not confirmed")
            checkable: true
            checked: strip.editor.unconfirmed
            enabled: strip.editor.editable
            onTriggered: {
                strip.editor.toggleUnconfirmed()
                checked = Qt.binding(() => strip.editor.unconfirmed)
            }
        }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            objectName: "foldedMovingTags"
            visible: moreMenu.translation
            iconRole: "moving-tags"
            text: qsTranslate("HikariSub.Translation", "Tags from original")
            checkable: true
            checked: strip.editor.moveTags
            onTriggered: {
                strip.app.setMoveTags(checked)
                checked = Qt.binding(() => strip.editor.moveTags)
            }
        }
        FoldSeparator {
            id: alignSlot // Text position's submenu goes after it
            visible: moreMenu.translation && (moreMenu.align || moreMenu.custom)
        }
        FoldSeparator { visible: moreMenu.align && moreMenu.custom }
        Instantiator {
            model: moreMenu.custom ? strip.tagButtons.buttons : []
            delegate: ShellMenuItem {
                height: visible ? implicitHeight : 0
                required property var modelData
                required property int index
                objectName: "foldedTagButton" + index
                iconRole: modelData.icon
                text: modelData.name.length > 0 ? modelData.name : modelData.tag
                shortcutText: strip.shellRoot.boundKeys("EDITBOX_TAG_BUTTON" + (index + 1), 2)
                enabled: strip.editor.editable
                onTriggered: strip.runTagButton(more, index, modelData, moreMenu.field)
            }
            onObjectAdded: (index, object) => moreMenu.insertItem(moreMenu.customStart() + index, object)
            onObjectRemoved: (index, object) => moreMenu.removeItem(object)
        }
        ShellMenuItem {
            height: visible ? implicitHeight : 0
            id: foldedAdd
            objectName: "foldedAddTagButton"
            visible: moreMenu.custom && !addTagButton.stripHidden
            iconRole: "add"
            text: qsTr("Add a tag button")
            onTriggered: tagButtonEditor.openFor(more, -1, null)
        }
        onAboutToShow: placeAlignment()
        // Text position's submenu is in the menu only while folded.
        function placeAlignment() {
            for (let i = 0; i < count; ++i) {
                if (itemAt(i) && itemAt(i).subMenu === alignMenu) {
                    if (!align)
                        takeMenu(i)
                    return
                }
            }
            if (!align)
                return
            for (let i = 0; i < count; ++i) {
                if (itemAt(i) === alignSlot) {
                    insertMenu(i + 1, alignMenu)
                    return
                }
            }
        }
        // The custom buttons' items go before Add.
        function customStart() {
            for (let i = 0; i < count; ++i) {
                if (itemAt(i) === foldedAdd)
                    return i
            }
            return count
        }
    }
    // Text position as a submenu of More (moreMenu.placeAlignment).
    ShellMenu {
        id: alignMenu
        objectName: "foldedAlignment"
        title: alignment.text
        iconRole: alignment.iconRole
        enabled: alignment.enabled
        Instantiator {
            model: [7, 8, 9, 4, 5, 6, 1, 2, 3] // the numpad's rows
            delegate: ShellMenuItem {
                required property int modelData
                objectName: "foldedAlignment" + modelData
                iconRole: alignment.roleFor(modelData)
                text: alignment.names[modelData]
                checkable: true
                checked: alignment.current === modelData
                onTriggered: alignment.choose(modelData)
            }
            onObjectAdded: (index, object) => alignMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => alignMenu.removeItem(object)
        }
    }
}
