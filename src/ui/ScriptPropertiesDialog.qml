// Y3: legacy ScriptInfo "ASS subtitle properties" (GLOBAL_OPEN_ASS_PROPERTIES).
// Only the fields the user changes are written (legacy IsModified), with the
// legacy defaults applied on Save.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "scriptPropertiesDialog"
    required property var app
    title: qsTr("ASS subtitle properties")
    // K1: the title with the set's script-properties icon (legacy ScriptInfo SetIcon(ASSPROPS)).
    header: IconDialogHeader { objectName: "scriptPropertiesDialogTitle"; iconRole: "script-properties"; text: dialog.title }
    modal: true
    property var initial: ({})
    property var edits: ({})

    // False for a Document that is not ASS.
    function openFor() {
        const p = app.scriptProperties()
        if (p.title === undefined)
            return false
        initial = p
        edits = ({})
        titleField.text = p.title
        scriptField.text = p.originalScript
        translationField.text = p.originalTranslation
        editingField.text = p.originalEditing
        timingField.text = p.originalTiming
        updatedField.text = p.updatedBy
        widthField.value = p.playResX
        heightField.value = p.playResY
        layoutWidthField.value = p.layoutResX
        layoutHeightField.value = p.layoutResY
        linkBox.checked = p.linkResolutions
        matrixBox.model = p.matrices
        matrixBox.currentIndex = p.matrix
        wrapBox.currentIndex = Math.max(0, Math.min(3, p.wrapStyle))
        collisionBox.currentIndex = p.reverseCollisions ? 1 : 0
        borderBox.checked = p.scaledBorderAndShadow
        edits = ({})
        open()
        return true
    }
    function mark(name) {
        const e = Object.assign({}, edits)
        e[name] = true
        edits = e
    }
    function values() {
        return { title: titleField.text, originalScript: scriptField.text, originalTranslation: translationField.text,
                 originalEditing: editingField.text, originalTiming: timingField.text, updatedBy: updatedField.text,
                 playResX: widthField.value, playResY: heightField.value,
                 layoutResX: layoutWidthField.value, layoutResY: layoutHeightField.value,
                 matrix: matrixBox.currentIndex, wrapStyle: wrapBox.currentIndex,
                 reverseCollisions: collisionBox.currentIndex === 1, scaledBorderAndShadow: borderBox.checked }
    }
    footer: DialogButtonBox {
        Button { objectName: "scriptPropertiesSave"; text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    onAccepted: app.applyScriptProperties(values(), edits, linkBox.checked)

    ColumnLayout {
        anchors.fill: parent
        GroupBox {
            title: qsTr("Subtitle information")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 2
                Label { text: qsTr("Title") }
                TextField { id: titleField; objectName: "propTitle"; Layout.fillWidth: true; Accessible.name: qsTr("Title"); onTextEdited: dialog.mark("title") }
                Label { text: qsTr("Author") }
                TextField { id: scriptField; objectName: "propAuthor"; Layout.fillWidth: true; Accessible.name: qsTr("Author"); onTextEdited: dialog.mark("originalScript") }
                Label { text: qsTr("Translator") }
                TextField { id: translationField; objectName: "propTranslator"; Layout.fillWidth: true; Accessible.name: qsTr("Translator"); onTextEdited: dialog.mark("originalTranslation") }
                Label { text: qsTr("Proofreading") }
                TextField { id: editingField; Layout.fillWidth: true; Accessible.name: qsTr("Proofreading"); onTextEdited: dialog.mark("originalEditing") }
                Label { text: qsTr("Timer") }
                TextField { id: timingField; Layout.fillWidth: true; Accessible.name: qsTr("Timer"); onTextEdited: dialog.mark("originalTiming") }
                Label { text: qsTr("Editing") }
                TextField { id: updatedField; Layout.fillWidth: true; Accessible.name: qsTr("Editing"); onTextEdited: dialog.mark("updatedBy") }
            }
        }
        GroupBox {
            title: qsTr("Resolution")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 5
                Label { text: qsTr("Subtitles") }
                SpinBox { id: widthField; objectName: "propWidth"; from: 0; to: 100000; editable: true; Accessible.name: qsTr("Video width"); onValueModified: dialog.mark("playResX") }
                Label { text: "×" }
                SpinBox { id: heightField; objectName: "propHeight"; from: 0; to: 100000; editable: true; Accessible.name: qsTr("Video height"); onValueModified: dialog.mark("playResY") }
                Button {
                    text: qsTr("From video")
                    enabled: dialog.initial.videoWidth > 0
                    onClicked: {
                        widthField.value = dialog.initial.videoWidth
                        heightField.value = dialog.initial.videoHeight
                        dialog.mark("playResX")
                        dialog.mark("playResY")
                    }
                }
                Label { text: qsTr("Layout") }
                SpinBox { id: layoutWidthField; from: 0; to: 100000; editable: true; enabled: !linkBox.checked; Accessible.name: qsTr("Layout width"); onValueModified: dialog.mark("layoutResX") }
                Label { text: "×" }
                SpinBox { id: layoutHeightField; from: 0; to: 100000; editable: true; enabled: !linkBox.checked; Accessible.name: qsTr("Layout height"); onValueModified: dialog.mark("layoutResY") }
                Button {
                    text: qsTr("From video")
                    enabled: !linkBox.checked && dialog.initial.videoWidth > 0
                    onClicked: {
                        layoutWidthField.value = dialog.initial.videoWidth
                        layoutHeightField.value = dialog.initial.videoHeight
                        dialog.mark("layoutResX")
                        dialog.mark("layoutResY")
                    }
                }
                CheckBox { id: linkBox; objectName: "propLinkResolutions"; text: qsTr("Link the layout resolution to the subtitle resolution"); Layout.columnSpan: 5 }
                Label { text: qsTr("YCbCr matrix") }
                ComboBox { id: matrixBox; objectName: "propMatrix"; Layout.columnSpan: 4; Accessible.name: qsTr("YCbCr matrix") }
            }
        }
        GroupBox {
            title: qsTr("Options")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 2
                Label { text: qsTr("Wrap style") }
                ComboBox {
                    id: wrapBox
                    objectName: "propWrapStyle"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Wrap style")
                    model: [qsTr("0: Auto, top line wider"), qsTr("1: End-of-line wrapping, only \\N breaks"),
                            qsTr("2: No wrapping, both \\n and \\N break"), qsTr("3: Auto, bottom line wider")]
                }
                Label { text: qsTr("Colliding lines") }
                ComboBox { id: collisionBox; objectName: "propCollisions"; Layout.fillWidth: true; Accessible.name: qsTr("Colliding lines"); model: [qsTr("Normal"), qsTr("Reversed")] }
                CheckBox { id: borderBox; objectName: "propScaleBorder"; text: qsTr("Scale border and shadow"); Layout.columnSpan: 2 }
            }
        }
    }
}
