pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: lines
    required property var app
    property int whichLines: 0
    property string styles: ""
    signal chose(int whichLines, string styles)
    readonly property int byStyles: 5
    spacing: 4

    function chooseStyles() {
        stylesPopup.before = { whichLines: whichLines, styles: styles }
        stylesPopup.openFor(pick, app.styleNames(), styles.split(",").map(n => n.trim()).filter(n => n !== ""),
                            { counts: app.styleLineCounts() })
    }

    ComboBox {
        id: which
        objectName: lines.objectName + "Which"
        Layout.fillWidth: true
        Accessible.name: qsTr("Lines")
        model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("Selected line and below"),
                qsTr("Starting at or after the selection"), qsTr("Starting at or before the selection"),
                qsTr("Lines in chosen styles…")]
        currentIndex: lines.whichLines
        onActivated: index => {
            if (index === lines.byStyles && lines.styles === "")
                lines.chooseStyles()
            else
                lines.chose(index, lines.styles)
        }
    }
    RowLayout {
        visible: lines.whichLines === lines.byStyles
        spacing: 4
        Layout.fillWidth: true
        Label {
            objectName: lines.objectName + "Styles"
            text: lines.styles.split(",").map(n => n.trim()).filter(n => n !== "").join(", ")
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Layout.fillWidth: true
        }
        Button {
            id: pick
            objectName: lines.objectName + "ChooseStyles"
            text: qsTr("Choose styles…")
            onClicked: lines.chooseStyles()
        }
    }
    ChooseStyles {
        id: stylesPopup
        objectName: lines.objectName + "StylesPopup"
        property var before: null
        onChanged: (names, reverted) => {
            if (reverted && before) {
                lines.chose(before.whichLines, before.styles)
                return
            }
            const chosen = names.join(",")
            lines.chose(chosen !== "" ? lines.byStyles : lines.whichLines, chosen)
        }
    }
}
