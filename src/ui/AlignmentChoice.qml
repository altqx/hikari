// E4: the "Text position" choice (legacy EditBox's Ban, EditBox.cpp:156-206):
// the nine \an positions. It shows the shown Line's position
// (EditBox::SetAlignment) and a choice puts \an<n> into the text's first
// block (EditBox::OnAnChoice); only ASS has it (EditBox::HideControls).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: alignment
    required property LineEditorController editor
    // After a choice the text field takes the focus (PutTagInText's SetFocus).
    signal chosen()
    spacing: 2
    enabled: editor.editable && editor.assFields

    Icon {
        iconRole: "alignment"
        Layout.alignment: Qt.AlignVCenter
    }
    ComboBox {
        id: choice
        objectName: "alignmentChoice"
        model: [qsTr("Bottom-left") + " (an1)", qsTr("Bottom-center") + " (an2)", qsTr("Bottom-right") + " (an3)",
                qsTr("Middle-left") + " (an4)", qsTr("Center") + " (an5)", qsTr("Middle-right") + " (an6)",
                qsTr("Top-left") + " (an7)", qsTr("Top-center") + " (an8)", qsTr("Top-right") + " (an9)"]
        currentIndex: alignment.editor.alignmentIndex
        // As wide as its longest choice (the closed box cut "Bottom-center (an2)").
        implicitContentWidthPolicy: ComboBox.WidestText
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Text position")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Text position")
        ToolTip.delay: 500
        onActivated: index => {
            alignment.editor.chooseAlignment(index)
            currentIndex = Qt.binding(() => alignment.editor.alignmentIndex)
            alignment.chosen()
        }
    }
}
