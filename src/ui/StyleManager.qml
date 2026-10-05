// Y1/Y2: legacy StyleStore "Style manager" with its StyleChange editor:
// the catalog ("Styles stored:") above, the Document's Styles ("Styles in
// ASS file:") below, the transfers between them, and the Style editor with
// its preview at the side.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel // K2: a Window draws white unless told
    id: manager
    objectName: "styleManager"
    // K1: the set's styles icon as the window's (legacy stylestore SetIcon).
    Component.onCompleted: IconTheme.setWindowIcon(manager, "styles")
    required property var styles   // StyleManagerController
    required property var catalogs // Y6: FontCatalogsController
    required property var app
    title: qsTr("Style manager")
    width: 980
    height: 760
    flags: Qt.Dialog

    property var storeSelected: []
    property var assSelected: []
    property bool editingStore: false
    property bool editing: false
    property var values: ({})

    function showFor(styleName) {
        styles.refreshLists()
        const row = styles.documentStyles.indexOf(styleName)
        assSelected = [Math.max(0, row)]
        storeSelected = styles.storeStyles.length > 0 ? [0] : []
        show()
        raise()
        requestActivate()
    }
    function closeManager() {
        styles.saveCatalog() // OnClose saves the store
        endEditing()
        close()
    }
    function sorted(list) { return list.slice().sort((a, b) => a - b) }

    // --- Conflict questions, asked one after another --------------------
    property var pendingNames: []
    property var pendingAnswers: []
    property var pendingDone: null
    function askReplace(names, done) {
        pendingNames = names
        pendingAnswers = []
        pendingDone = done
        nextQuestion()
    }
    function nextQuestion() {
        const last = pendingAnswers.length > 0 ? pendingAnswers[pendingAnswers.length - 1] : ""
        if (pendingAnswers.length >= pendingNames.length || last === "yesToAll" || last === "cancel") {
            const done = pendingDone
            pendingDone = null
            done(pendingAnswers)
            return
        }
        replaceQuestion.name = pendingNames[pendingAnswers.length]
        replaceQuestion.open()
    }
    function answer(a) {
        replaceQuestion.close()
        pendingAnswers = pendingAnswers.concat([a])
        nextQuestion()
    }
    Dialog {
        id: replaceQuestion
        objectName: "styleReplaceQuestion"
        property string name: ""
        title: qsTr("Confirmation")
        modal: true
        anchors.centerIn: parent
        Label { text: qsTr("Style named \"%1\" already exists. Replace?").arg(replaceQuestion.name) }
        footer: RowLayout {
            Button { objectName: "replaceYesToAll"; text: qsTr("Yes to all"); onClicked: manager.answer("yesToAll") }
            Button { objectName: "replaceYes"; text: qsTr("Yes"); onClicked: manager.answer("yes") }
            Button { objectName: "replaceNo"; text: qsTr("No"); onClicked: manager.answer("no") }
            Button { text: qsTr("Cancel"); onClicked: manager.answer("cancel") }
        }
    }
    Dialog {
        id: message
        objectName: "styleMessage"
        property alias text: messageLabel.text
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: messageLabel }
    }

    // --- Editor ------------------------------------------------------------
    function beginEditing(map, store) {
        if (map.name === undefined)
            return
        editingStore = store
        values = map
        // Y6: StyleChange makes its font list once (ChangeCatalog in its constructor).
        if (!fontCatalogBar.made) {
            fontCatalogBar.made = true
            fontCatalogBar.reset()
        }
        editor.load(map)
        editing = true
        updatePreview()
    }
    function endEditing() {
        editing = false
        styles.endEdit()
    }
    function updatePreview() {
        if (editing)
            styles.renderPreview(editor.values(), Math.max(1, preview.width), Math.max(1, preview.height), previewText.text)
    }
    // CommitChange: the multi-edit question, then ChangeStyle and the rename question.
    function commit(close) {
        const v = editor.values()
        const q = styles.commitQuestions(v)
        const selected = editingStore ? storeSelected : assSelected
        const finish = function(applyAll) {
            const done = function(rename) {
                const problem = styles.commitEdit(v, selected, applyAll, rename)
                if (problem !== "") {
                    message.text = problem
                    message.open()
                    return
                }
                if (close)
                    endEditing()
            }
            if (q.rename) {
                renameQuestion.done = done
                renameQuestion.open()
            } else {
                done(false)
            }
        }
        if (q.fields && selected.length > 1) {
            multiQuestion.done = finish
            multiQuestion.open()
        } else {
            finish(false)
        }
    }
    Dialog {
        id: multiQuestion
        objectName: "styleMultiQuestion"
        property var done: null
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Change all selected styles?") }
        onAccepted: done(true)
        onRejected: done(false)
    }
    Dialog {
        id: renameQuestion
        objectName: "styleRenameQuestion"
        property var done: null
        title: qsTr("Confirmation")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Style name modified. Do you want to change all instances to the new name?") }
        onAccepted: done(true)
        onRejected: done(false)
    }

    component StyleList: ListView {
        id: list
        required property var names
        required property bool store
        property var selected: []
        signal picked(var rows)
        signal edit()
        clip: true
        model: names
        Accessible.role: Accessible.List
        Accessible.name: store ? qsTr("Styles stored:") : qsTr("Styles in ASS file:")
        keyNavigationEnabled: true
        delegate: Rectangle {
            required property int index
            required property string modelData
            width: list.width
            height: label.implicitHeight + 6
            color: list.selected.indexOf(index) >= 0 ? palette.highlight : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData
            Accessible.selected: list.selected.indexOf(index) >= 0
            Label {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                x: 4
                text: modelData
                color: list.selected.indexOf(index) >= 0 ? palette.highlightedText : palette.text
            }
            MouseArea {
                anchors.fill: parent
                onClicked: (mouse) => {
                    let rows = list.selected.slice()
                    if (mouse.modifiers & Qt.ControlModifier) {
                        const at = rows.indexOf(index)
                        if (at >= 0) rows.splice(at, 1); else rows.push(index)
                    } else if ((mouse.modifiers & Qt.ShiftModifier) && rows.length > 0) {
                        const from = rows[0]
                        rows = []
                        for (let i = Math.min(from, index); i <= Math.max(from, index); ++i) rows.push(i)
                    } else {
                        rows = [index]
                    }
                    list.picked(rows.sort((a, b) => a - b))
                }
                onDoubleClicked: { list.picked([index]); list.edit() }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        ColumnLayout {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            GroupBox {
                title: qsTr("Catalog:")
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    ComboBox {
                        id: catalogBox
                        objectName: "styleCatalog"
                        Layout.fillWidth: true
                        model: manager.styles.catalogs
                        currentIndex: manager.styles.catalogs.indexOf(manager.styles.catalog)
                        ToolTip.text: qsTr("Style catalog")
                        onActivated: (i) => { manager.styles.chooseCatalog(model[i]); manager.storeSelected = [0] }
                    }
                    Button { objectName: "newCatalog"; text: qsTr("New"); ToolTip.text: qsTr("New style catalog"); onClicked: newCatalog.open() }
                    Button {
                        objectName: "deleteCatalog"
                        text: qsTr("Delete")
                        ToolTip.text: qsTr("Delete selected style catalog")
                        enabled: manager.styles.catalog !== "Default"
                        onClicked: deleteCatalogQuestion.open()
                    }
                }
            }
            GroupBox {
                title: qsTr("Styles stored:")
                Layout.fillWidth: true
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    StyleList {
                        id: storeList
                        objectName: "storeStyles"
                        names: manager.styles.storeStyles
                        store: true
                        selected: manager.storeSelected
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPicked: (rows) => manager.storeSelected = rows
                        onEdit: manager.beginEditing(manager.styles.beginEdit(true, manager.storeSelected[0]), true)
                    }
                    ColumnLayout {
                        Button { objectName: "storeNew"; text: qsTr("New"); ToolTip.text: qsTr("Create new style in storage"); onClicked: manager.beginEditing(manager.styles.beginNew(true), true) }
                        Button { objectName: "storeCopy"; text: qsTr("Copy"); enabled: manager.storeSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginCopy(true, manager.storeSelected[0]), true) }
                        Button { objectName: "storeEdit"; text: qsTr("Edit"); enabled: manager.storeSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginEdit(true, manager.storeSelected[0]), true) }
                        Button { text: qsTr("Load"); ToolTip.text: qsTr("Load style from external ASS file to storage"); onClicked: { loadDialog.toStore = true; loadDialog.open() } }
                        Button { objectName: "storeDelete"; text: qsTr("Delete"); enabled: manager.storeSelected.length > 0; onClicked: manager.storeSelected = manager.styles.removeStyles(true, manager.storeSelected) }
                        Button { text: qsTr("Sort"); onClicked: { manager.styles.sortStyles(true); manager.storeSelected = [0] } }
                        Button { text: "⇈"; Accessible.name: qsTr("Move selected styles to beginning"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 0) }
                        Button { text: "↑"; Accessible.name: qsTr("Move selected styles up"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 1) }
                        Button { text: "↓"; Accessible.name: qsTr("Move selected styles down"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 2) }
                        Button { text: "⇊"; Accessible.name: qsTr("Move selected styles to end"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 3) }
                    }
                }
            }
            RowLayout {
                Button {
                    objectName: "addToStore"
                    text: "↑ " + qsTr("Add to storage")
                    ToolTip.text: qsTr("Copy style from ASS to storage")
                    enabled: manager.assSelected.length > 0
                    onClicked: {
                        const rows = manager.assSelected
                        manager.askReplace(manager.styles.transferConflicts(true, rows), function(answers) {
                            manager.storeSelected = manager.styles.addToStore(rows, answers)
                        })
                    }
                }
                Button {
                    objectName: "addToAss"
                    text: "↓ " + qsTr("Add to ASS")
                    ToolTip.text: qsTr("Copy style from storage to ASS")
                    enabled: manager.storeSelected.length > 0 && manager.styles.available
                    onClicked: {
                        const rows = manager.storeSelected
                        manager.askReplace(manager.styles.transferConflicts(false, rows), function(answers) {
                            manager.assSelected = manager.styles.addToDocument(rows, answers)
                        })
                    }
                }
                Button {
                    objectName: "addToAllAss"
                    text: "⇊ " + qsTr("Add to all open ASS files")
                    enabled: manager.storeSelected.length > 0
                    onClicked: {
                        const rows = manager.storeSelected
                        // Legacy asks once per conflicting name in each Document; the
                        // active Document's questions stand for all (its answers are reused).
                        manager.askReplace(manager.styles.transferConflicts(false, rows), function(answers) {
                            manager.styles.addToAllDocuments(rows, answers)
                        })
                    }
                }
            }
            GroupBox {
                title: qsTr("Styles in ASS file:")
                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: manager.styles.available
                RowLayout {
                    anchors.fill: parent
                    StyleList {
                        id: assList
                        objectName: "assStyles"
                        names: manager.styles.documentStyles
                        store: false
                        selected: manager.assSelected
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPicked: (rows) => manager.assSelected = rows
                        onEdit: manager.beginEditing(manager.styles.beginEdit(false, manager.assSelected[0]), false)
                    }
                    ColumnLayout {
                        Button { objectName: "assNew"; text: qsTr("New"); ToolTip.text: qsTr("Create new ASS style"); onClicked: manager.beginEditing(manager.styles.beginNew(false), false) }
                        Button { objectName: "assCopy"; text: qsTr("Copy"); enabled: manager.assSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginCopy(false, manager.assSelected[0]), false) }
                        Button { objectName: "assEdit"; text: qsTr("Edit"); enabled: manager.assSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginEdit(false, manager.assSelected[0]), false) }
                        Button { text: qsTr("Load"); ToolTip.text: qsTr("Load style from external ASS file"); onClicked: { loadDialog.toStore = false; loadDialog.open() } }
                        Button { objectName: "assDelete"; text: qsTr("Delete"); enabled: manager.assSelected.length > 0; onClicked: manager.assSelected = manager.styles.removeStyles(false, manager.assSelected) }
                        Button { text: qsTr("Sort"); onClicked: { manager.styles.sortStyles(false); manager.assSelected = [0] } }
                        Button {
                            objectName: "assClean"
                            text: qsTr("Clear")
                            ToolTip.text: qsTr("Delete unused ASS styles")
                            onClicked: { message.title = qsTr("Status of deleted styles"); message.text = manager.styles.cleanStyles(); message.open() }
                        }
                        Button { text: "⇈"; Accessible.name: qsTr("Move selected styles to beginning"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 0) }
                        Button { text: "↑"; Accessible.name: qsTr("Move selected styles up"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 1) }
                        Button { text: "↓"; Accessible.name: qsTr("Move selected styles down"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 2) }
                        Button { text: "⇊"; Accessible.name: qsTr("Move selected styles to end"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 3) }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button { objectName: "styleManagerOk"; text: "OK"; onClicked: { if (manager.editing) manager.commit(true); else manager.closeManager() } }
                Button { objectName: "styleManagerClose"; text: qsTr("Close"); onClicked: manager.closeManager() }
            }
        }

        // StyleChange: the editor, shown while a Style is edited.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: manager.editing
            ColumnLayout {
                id: editor
                objectName: "styleEditor"
                width: parent.width
                function load(m) {
                    nameField.text = m.name
                    fontField.editText = m.fontname
                    sizeField.text = m.fontsize
                    boldBox.checked = m.bold
                    italicBox.checked = m.italic
                    underlineBox.checked = m.underline
                    strikeBox.checked = m.strikeOut
                    colours.itemAt(0).colour = m.primary
                    colours.itemAt(1).colour = m.secondary
                    colours.itemAt(2).colour = m.outline
                    colours.itemAt(3).colour = m.back
                    outlineField.text = m.outlineWidth
                    shadowField.text = m.shadow
                    scaleXField.text = m.scaleX
                    scaleYField.text = m.scaleY
                    angleField.text = m.angle
                    spacingField.text = m.spacing
                    borderBox.checked = m.borderStyle
                    alignments.itemAt([7, 8, 9, 4, 5, 6, 1, 2, 3].indexOf(Number(m.alignment))).checked = true
                    marginLeft.text = m.marginLeft
                    marginRight.text = m.marginRight
                    marginVertical.text = m.marginVertical
                    // An unknown encoding shows "1 - Default" (legacy).
                    let enc = encodings.model.findIndex(e => e.startsWith(m.encoding + " "))
                    encodings.currentIndex = enc < 0 ? 1 : enc
                }
                function values() {
                    let alignment = "2"
                    for (let i = 0; i < 9; ++i)
                        if (alignments.itemAt(i).checked)
                            alignment = String([7, 8, 9, 4, 5, 6, 1, 2, 3][i])
                    return {
                        name: nameField.text, fontname: fontField.editText, fontsize: sizeField.text,
                        primary: colours.itemAt(0).colour, secondary: colours.itemAt(1).colour,
                        outline: colours.itemAt(2).colour, back: colours.itemAt(3).colour,
                        bold: boldBox.checked, italic: italicBox.checked, underline: underlineBox.checked,
                        strikeOut: strikeBox.checked, outlineWidth: outlineField.text, shadow: shadowField.text,
                        scaleX: scaleXField.text, scaleY: scaleYField.text, angle: angleField.text,
                        spacing: spacingField.text, borderStyle: borderBox.checked, alignment: alignment,
                        marginLeft: marginLeft.text, marginRight: marginRight.text, marginVertical: marginVertical.text,
                        encoding: encodings.currentText.split(" ")[0]
                    }
                }
                GroupBox {
                    title: qsTr("Style name:")
                    Layout.fillWidth: true
                    TextField {
                        id: nameField
                        objectName: "styleName"
                        width: parent.width
                        Accessible.name: qsTr("Style name:")
                        validator: RegularExpressionValidator { regularExpression: /[^,]*/ } // legacy excludes ","
                        onTextEdited: manager.updatePreview()
                    }
                }
                GroupBox {
                    title: qsTr("Font and size:")
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        RowLayout {
                            ComboBox {
                                id: fontField
                                objectName: "styleFont"
                                editable: true
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Font")
                                onEditTextChanged: manager.updatePreview()
                            }
                            TextField { id: sizeField; objectName: "styleSize"; Layout.preferredWidth: 66; Accessible.name: qsTr("Size"); onTextEdited: manager.updatePreview() }
                        }
                        // Y6: the font catalogs and the Filter (StyleChange's filtersizer).
                        FontCatalogBar {
                            id: fontCatalogBar
                            property bool made: false
                            Layout.fillWidth: true
                            catalogs: manager.catalogs
                            fontName: fontField.editText
                            // HikariChoice::PutArray keeps the typed font.
                            onListMade: {
                                const text = fontField.editText
                                fontField.model = fonts
                                fontField.editText = text
                            }
                        }
                        RowLayout {
                            CheckBox { id: boldBox; objectName: "styleBold"; text: qsTr("Bold"); onToggled: manager.updatePreview() }
                            CheckBox { id: italicBox; text: qsTr("Italic"); onToggled: manager.updatePreview() }
                            CheckBox { id: underlineBox; text: qsTr("Underline"); onToggled: manager.updatePreview() }
                            CheckBox { id: strikeBox; text: qsTr("Strikethrough"); onToggled: manager.updatePreview() }
                        }
                    }
                }
                GroupBox {
                    title: qsTr("Colors and transparency:")
                    Layout.fillWidth: true
                    RowLayout {
                        Repeater {
                            id: colours
                            model: [qsTr("First"), qsTr("Second"), qsTr("Border"), qsTr("Shadow")]
                            ColumnLayout {
                                required property string modelData
                                property var colour: ({ r: 0, g: 0, b: 0, a: 0 })
                                Button {
                                    text: modelData
                                    Accessible.name: modelData
                                    background: Rectangle { color: Qt.rgba(colour.r / 255, colour.g / 255, colour.b / 255, 1); border.width: 1 }
                                    onClicked: { colourPicker.target = parent; colourPicker.selectedColor = Qt.rgba(colour.r / 255, colour.g / 255, colour.b / 255, 1); colourPicker.open() }
                                }
                                SpinBox {
                                    from: 0; to: 255; editable: true
                                    value: colour.a
                                    Accessible.name: modelData + " " + qsTr("transparency")
                                    onValueModified: { colour = { r: colour.r, g: colour.g, b: colour.b, a: value }; manager.updatePreview() }
                                }
                            }
                        }
                    }
                }
                GridLayout {
                    columns: 4
                    Label { text: qsTr("Border:") }
                    TextField { id: outlineField; Accessible.name: qsTr("Border:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Shadow:") }
                    TextField { id: shadowField; Accessible.name: qsTr("Shadow:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Scale X:") }
                    TextField { id: scaleXField; Accessible.name: qsTr("Scale X:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Scale Y:") }
                    TextField { id: scaleYField; Accessible.name: qsTr("Scale Y:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Angle:") }
                    TextField { id: angleField; Accessible.name: qsTr("Angle:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Spacing:") }
                    TextField { id: spacingField; Accessible.name: qsTr("Spacing:"); onTextEdited: manager.updatePreview() }
                    Label { text: qsTr("Border type:") }
                    CheckBox { id: borderBox; text: qsTr("Opaque box"); onToggled: manager.updatePreview() }
                }
                GridLayout {
                    columns: 6
                    Label { text: qsTr("Left margin:") }
                    TextField { id: marginLeft; Accessible.name: qsTr("Left margin:"); Layout.preferredWidth: 60 }
                    Label { text: qsTr("Right:") }
                    TextField { id: marginRight; Accessible.name: qsTr("Right:"); Layout.preferredWidth: 60 }
                    Label { text: qsTr("Vertical:") }
                    TextField { id: marginVertical; Accessible.name: qsTr("Vertical:"); Layout.preferredWidth: 60 }
                }
                RowLayout {
                    GroupBox {
                        title: qsTr("Alignment:")
                        GridLayout {
                            columns: 3
                            ButtonGroup { id: alignmentGroup }
                            Repeater {
                                id: alignments
                                model: ["7", "8", "9", "4", "5", "6", "1", "2", "3"]
                                RadioButton { required property string modelData; text: modelData; ButtonGroup.group: alignmentGroup; onToggled: manager.updatePreview() }
                            }
                        }
                    }
                    GroupBox {
                        title: qsTr("Text encoding:")
                        ComboBox {
                            id: encodings
                            model: [qsTr("0 - ANSI"), qsTr("1 - Default"), qsTr("2 - Symbol"), qsTr("77 - Mac"), qsTr("128 - Japanese"),
                                    qsTr("129 - Hangul"), qsTr("130 - Johab"), qsTr("134 - Chinese GB2312"), qsTr("135 - Chinese BIG5"),
                                    qsTr("161 - Greek"), qsTr("162 - Turkish"), qsTr("163 - Vietnamese"), qsTr("177 - Hebrew"),
                                    qsTr("178 - Arabic"), qsTr("186 - Baltic"), qsTr("204 - Russian"), qsTr("222 - Thai"),
                                    qsTr("238 - Central European (Polish)"), qsTr("255 - OEM")]
                        }
                    }
                }
                GroupBox {
                    title: qsTr("Style preview:")
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        Image {
                            id: preview
                            objectName: "stylePreview"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            cache: false
                            source: manager.editing ? "image://stylepreview/" + manager.styles.previewKey : ""
                            onWidthChanged: manager.updatePreview()
                        }
                        // Legacy STYLE_PREVIEW_TEXT, edited by clicking the preview.
                        TextField { id: previewText; text: "Podgląd"; Accessible.name: qsTr("Preview text"); Layout.fillWidth: true; onTextEdited: manager.updatePreview() }
                    }
                }
                RowLayout {
                    Button { objectName: "styleEditorOk"; text: "Ok"; onClicked: manager.commit(true) }
                    Button { objectName: "styleEditorApply"; text: qsTr("Apply"); onClicked: manager.commit(false) }
                    Button { text: qsTr("Cancel"); onClicked: manager.endEditing() }
                }
            }
        }
    }

    ColorDialog {
        id: colourPicker
        property var target: null
        title: qsTr("Choose color")
        onAccepted: {
            const c = selectedColor
            target.colour = { r: Math.round(c.r * 255), g: Math.round(c.g * 255), b: Math.round(c.b * 255), a: target.colour.a }
            manager.updatePreview()
        }
    }
    Dialog {
        id: newCatalog
        objectName: "newCatalogDialog"
        title: qsTr("New catalog")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: catalogName; objectName: "newCatalogName"; Accessible.name: qsTr("Catalog name") }
        onAccepted: { manager.styles.createCatalog(catalogName.text); catalogName.text = ""; manager.storeSelected = [] }
    }
    Dialog {
        id: deleteCatalogQuestion
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Do you really want to delete the catalog named \"%1\"?").arg(manager.styles.catalog) }
        onAccepted: manager.styles.deleteCatalog(manager.styles.catalog)
    }
    // LoadStylesS: "Choose ASS file", then the Styles to load.
    FileDialog {
        id: loadDialog
        property bool toStore: false
        title: qsTr("Choose ASS file")
        nameFilters: [qsTr("ASS subtitle files(*.ass)") + " (*.ass)"]
        onAccepted: { chooseLoaded.file = selectedFile; chooseLoaded.names = manager.styles.fileStyles(selectedFile); chooseLoaded.checkedNames = []; chooseLoaded.open() }
    }
    Dialog {
        id: chooseLoaded
        objectName: "chooseLoadedStyles"
        property url file
        property var names: []
        property var checkedNames: []
        title: qsTr("Choose styles")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Repeater {
                model: chooseLoaded.names
                CheckBox {
                    required property string modelData
                    text: modelData
                    onToggled: chooseLoaded.checkedNames = checked ? chooseLoaded.checkedNames.concat([modelData])
                                                                   : chooseLoaded.checkedNames.filter(n => n !== modelData)
                }
            }
        }
        onAccepted: {
            const names = chooseLoaded.names.filter(n => chooseLoaded.checkedNames.indexOf(n) >= 0)
            const toStore = loadDialog.toStore
            manager.askReplace(manager.styles.fileConflicts(toStore, chooseLoaded.file, names), function(answers) {
                manager.styles.loadFromFile(toStore, chooseLoaded.file, names, answers)
            })
        }
    }
}
