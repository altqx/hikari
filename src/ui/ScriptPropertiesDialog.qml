pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.ScriptProperties"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "scriptPropertiesDialog"
    required property var app
    title: qsTr("ASS file properties")
    dialogId: "scriptProperties"
    modal: true
    defaultButton: saveButton
    initialFocus: titleField
    property var initial: ({})
    property var edits: ({})
    property var document: 0
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }

    // MUST KILL: openFor returns false for a non-ASS Document, signalled by scriptProperties() having no title
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
        document = app.editingDocument()
        open()
        return true
    }
    function mark(name) {
        const e = Object.assign({}, edits)
        e[name] = true
        edits = e
    }
    property var video: null
    Connections {
        target: dialog.video
        function onChanged() { if (dialog.visible) dialog.refreshVideo() }
    }
    function refreshVideo() {
        const p = app.scriptProperties()
        if (p.title === undefined || (p.videoWidth === initial.videoWidth && p.videoHeight === initial.videoHeight))
            return
        initial = Object.assign({}, initial, { videoWidth: p.videoWidth, videoHeight: p.videoHeight })
    }
    function fromVideo(widthBox, heightBox, widthName, heightName) {
        const p = app.scriptProperties()
        if (!(p.videoWidth > 0))
            return
        widthBox.value = p.videoWidth
        heightBox.value = p.videoHeight
        mark(widthName)
        mark(heightName)
    }
    function values() {
        return { title: titleField.text, originalScript: scriptField.text, originalTranslation: translationField.text,
                 originalEditing: editingField.text, originalTiming: timingField.text, updatedBy: updatedField.text,
                 playResX: widthField.value, playResY: heightField.value,
                 layoutResX: layoutWidthField.value, layoutResY: layoutHeightField.value,
                 matrix: matrixBox.currentIndex, wrapStyle: wrapBox.currentIndex,
                 reverseCollisions: collisionBox.currentIndex === 1, scaledBorderAndShadow: borderBox.checked }
    }
    onAccepted: {
        if (app.editingDocument() === document)
            app.applyScriptProperties(values(), edits, linkBox.checked)
    }

    component FieldLabel: Label { Layout.preferredWidth: 104 }
    component WideField: TextField { Layout.fillWidth: true; Layout.preferredWidth: 300 }
    component SizeBox: SpinBox { from: 0; to: 100000; editable: true; Layout.preferredWidth: 110 }

    FormSection {
        objectName: "propCredits"
        title: qsTr("Credits")
        ruled: false
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true
            FieldLabel { text: qsTr("Title") }
            WideField { id: titleField; objectName: "propTitle"; Accessible.name: qsTr("Title"); onTextEdited: dialog.mark("title") }
            FieldLabel { text: qsTr("Author") }
            WideField { id: scriptField; objectName: "propAuthor"; Accessible.name: qsTr("Author"); onTextEdited: dialog.mark("originalScript") }
            FieldLabel { text: qsTr("Translator") }
            WideField { id: translationField; objectName: "propTranslator"; Accessible.name: qsTr("Translator"); onTextEdited: dialog.mark("originalTranslation") }
            FieldLabel { text: qsTr("Proofreading") }
            WideField { id: editingField; objectName: "propProofreading"; Accessible.name: qsTr("Proofreading"); onTextEdited: dialog.mark("originalEditing") }
            FieldLabel { text: qsTr("Timer") }
            WideField { id: timingField; objectName: "propTimer"; Accessible.name: qsTr("Timer"); onTextEdited: dialog.mark("originalTiming") }
            FieldLabel { text: qsTr("Editing") }
            WideField { id: updatedField; objectName: "propEditing"; Accessible.name: qsTr("Editing"); onTextEdited: dialog.mark("updatedBy") }
        }
    }
    FormSection {
        objectName: "propResolution"
        title: qsTr("Resolution")
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true
            FieldLabel { text: qsTr("Subtitles") }
            RowLayout {
                spacing: 6
                SizeBox { id: widthField; objectName: "propWidth"; Accessible.name: qsTr("Video width"); onValueModified: dialog.mark("playResX") }
                Label { text: "×" }
                SizeBox { id: heightField; objectName: "propHeight"; Accessible.name: qsTr("Video height"); onValueModified: dialog.mark("playResY") }
                Button {
                    objectName: "propFromVideo"
                    text: qsTr("From video")
                    enabled: dialog.initial.videoWidth > 0
                    onClicked: dialog.fromVideo(widthField, heightField, "playResX", "playResY")
                }
            }
            FieldLabel { text: qsTr("Layout") }
            RowLayout {
                spacing: 6
                SizeBox { id: layoutWidthField; objectName: "propLayoutWidth"; enabled: !linkBox.checked; Accessible.name: qsTr("Layout width"); onValueModified: dialog.mark("layoutResX") }
                Label { text: "×" }
                SizeBox { id: layoutHeightField; objectName: "propLayoutHeight"; enabled: !linkBox.checked; Accessible.name: qsTr("Layout height"); onValueModified: dialog.mark("layoutResY") }
                Button {
                    objectName: "propLayoutFromVideo"
                    text: qsTr("From video")
                    enabled: !linkBox.checked && dialog.initial.videoWidth > 0
                    onClicked: dialog.fromVideo(layoutWidthField, layoutHeightField, "layoutResX", "layoutResY")
                }
            }
            Item { implicitWidth: 1 }
            CheckBox { id: linkBox; objectName: "propLinkResolutions"; text: qsTr("Same as the subtitles") }
            FieldLabel { text: qsTr("YCbCr matrix") }
            ComboBox { id: matrixBox; objectName: "propMatrix"; Layout.preferredWidth: 200; Accessible.name: qsTr("YCbCr matrix"); onActivated: dialog.mark("matrix") }
        }
    }
    FormSection {
        objectName: "propRendering"
        title: qsTr("Rendering")
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true
            FieldLabel { text: qsTr("Wrap style") }
            ComboBox {
                id: wrapBox
                objectName: "propWrapStyle"
                Layout.fillWidth: true
                Accessible.name: qsTr("Wrap style")
                onActivated: dialog.mark("wrapStyle")
                model: [qsTr("0: Auto, top line wider"), qsTr("1: End-of-line wrapping, only %1 breaks").arg("\\N"),
                        qsTr("2: No wrapping, both %1 and %2 break").arg("\\n", "\\N"), qsTr("3: Auto, bottom line wider")]
            }
            FieldLabel { text: qsTr("Colliding lines") }
            ComboBox { id: collisionBox; objectName: "propCollisions"; Layout.preferredWidth: 200; Accessible.name: qsTr("Colliding lines"); model: [qsTr("Normal"), qsTr("Reversed")]; onActivated: dialog.mark("reverseCollisions") }
            Item { implicitWidth: 1 }
            CheckBox { id: borderBox; objectName: "propScaleBorder"; text: qsTr("Scale border and shadow"); onToggled: dialog.mark("scaledBorderAndShadow") }
        }
    }

    buttons: [
        PrimaryButton {
            id: saveButton
            objectName: "scriptPropertiesSave"
            text: qsTr("Save")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        },
        Button {
            objectName: "scriptPropertiesCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
