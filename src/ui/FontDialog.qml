// E1: the editor's font dialog (legacy FontDialog "Select a font"). Each
// change is applied to the edited text at once, as legacy's FONT_CHANGED
// does; Cancel takes every change back.
//
// Y6: the list is the FontService's families through the catalog choice and
// the Filter ("Filtering and font catalogs", FontDialog.cpp:422-486,
// 656-732); typing selects as FontList::SetSelectionByPartialName does; under
// the preview the renderer reports the face it selected for the name.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "fontDialog"
    required property LineEditorController editor
    required property var catalogs // Y6: FontCatalogsController
    title: qsTr("Select a font")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    property bool loading: false
    readonly property var families: catalogBar.fonts
    // editedStyle->Fontname: the font the list selects after ChangeCatalog
    // (the name opened with, then the name each change applied).
    property string editedName: ""
    property var resolution: ({})
    property int resolveRequest: -1

    // Opens on the field `role` with its selection; false when the editor refuses.
    function openFor(role, selectionStart, selectionEnd) {
        const f = editor.beginFont(role, selectionStart, selectionEnd)
        if (f.name === undefined)
            return false
        loading = true
        fontName.text = f.name
        fontSize.text = f.size
        bold.checked = f.bold
        italic.checked = f.italic
        underline.checked = f.underline
        strikeOut.checked = f.strikeOut
        editedName = f.name
        catalogBar.fontName = f.name
        catalogBar.reset() // the list, then SetSelectionByName(acst->Fontname)
        loading = false
        resolve()
        open()
        return true
    }
    function current() {
        return { name: fontName.text, size: fontSize.text, bold: bold.checked, italic: italic.checked,
                 underline: underline.checked, strikeOut: strikeOut.checked }
    }
    function changed() {
        if (!loading)
            changeTimer.restart()
    }
    function selectByName(name) {
        fontList.currentIndex = catalogs.nameIndex(families, name)
        fontList.positionViewAtIndex(Math.max(0, fontList.currentIndex), ListView.Center)
    }
    // Y6: what the renderer selects for the name (requested and resolved
    // identities stay distinct, fonts.md).
    function resolve() {
        resolveRequest = catalogs.resolveFamily(fontName.text, bold.checked, italic.checked)
    }
    Connections {
        target: dialog.catalogs
        function onResolutionReady(requestId, result) {
            if (requestId === dialog.resolveRequest)
                dialog.resolution = result
        }
    }
    function flush() {
        if (changeTimer.running) {
            changeTimer.stop()
            editor.changeFont(current())
        }
    }
    Timer {
        id: changeTimer
        interval: 100
        onTriggered: {
            dialog.editor.changeFont(dialog.current())
            dialog.editedName = fontName.text
            dialog.resolve()
        }
    }
    onAccepted: {
        flush()
        editor.endDialog(true)
    }
    onRejected: {
        changeTimer.stop()
        editor.endDialog(false)
    }

    ColumnLayout {
        anchors.fill: parent
        GroupBox {
            title: qsTr("Font")
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                ListView {
                    id: fontList
                    objectName: "fontList"
                    Layout.preferredWidth: 250
                    Layout.preferredHeight: 200
                    clip: true
                    model: dialog.families
                    Accessible.name: qsTr("Fonts")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        text: modelData
                        highlighted: ListView.isCurrentItem
                        onClicked: { // OnFontChanged
                            fontList.currentIndex = index
                            fontName.text = modelData
                        }
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    TextField {
                        id: fontName
                        objectName: "fontName"
                        Layout.preferredWidth: 150
                        Accessible.name: qsTr("Font name")
                        onTextChanged: {
                            // OnUpdateText: FontList::SetSelectionByPartialName.
                            if (!dialog.loading) {
                                fontList.currentIndex = dialog.catalogs.partialIndex(dialog.families, text)
                                fontList.positionViewAtIndex(Math.max(0, fontList.currentIndex), ListView.Contain)
                            }
                            catalogBar.fontName = text
                            dialog.changed()
                        }
                    }
                    TextField {
                        id: fontSize
                        objectName: "fontSize"
                        Layout.preferredWidth: 80
                        Accessible.name: qsTr("Font size")
                        validator: DoubleValidator { bottom: 1; top: 10000; notation: DoubleValidator.StandardNotation }
                        onTextChanged: if (acceptableInput) dialog.changed()
                    }
                    CheckBox { id: bold; objectName: "fontBold"; text: qsTr("Bold"); onToggled: dialog.changed() }
                    CheckBox { id: italic; objectName: "fontItalic"; text: qsTr("Italic"); onToggled: dialog.changed() }
                    CheckBox { id: underline; objectName: "fontUnderline"; text: qsTr("Underline"); onToggled: dialog.changed() }
                    CheckBox { id: strikeOut; objectName: "fontStrikeOut"; text: qsTr("Strikethrough"); onToggled: dialog.changed() }
                }
            }
        }
        GroupBox {
            title: qsTr("Filtering and font catalogs")
            Layout.fillWidth: true
            FontCatalogBar {
                id: catalogBar
                anchors.fill: parent
                catalogs: dialog.catalogs
                fontDialog: true
                // ChangeCatalog: Fonts->SetSelectionByName(editedStyle->Fontname).
                onListMade: dialog.selectByName(dialog.editedName)
            }
        }
        GroupBox {
            title: qsTr("Preview")
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            Label {
                anchors.fill: parent
                clip: true
                text: qsTr("AaBbCcDdEeFfGg 0123456789")
                font.family: fontName.text
                font.pixelSize: Math.min(64, Math.max(6, Number(fontSize.text) || 20))
                font.bold: bold.checked
                font.italic: italic.checked
                font.underline: underline.checked
                font.strikeout: strikeOut.checked
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }
        }
        Label {
            objectName: "fontResolution"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            readonly property var r: dialog.resolution
            text: r.kind === "requested" ? qsTr("The subtitle renderer uses %1 (%2).").arg(r.family).arg(r.file)
                : r.kind === "substituted" ? qsTr("Not installed under this name: the subtitle renderer substitutes %1 (%2).").arg(r.family).arg(r.file)
                : r.kind === "fallback" ? qsTr("Not installed: the subtitle renderer falls back to %1 (%2).").arg(r.family).arg(r.file)
                : r.kind === "missing" ? qsTr("No font answers this name.")
                : ""
        }
    }
}
