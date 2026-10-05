pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// P10 (#200): legacy's status bar (HikariStatusBar; its nine fields set up at
// HikariSubFrame.cpp:150-155), in the accepted Classic shell's status strip
// (docs/qt/ux/visual-language.md: padding 7 / 12, gap 14, the status text
// role). Legacy's order and widths: the first field, help and progress text,
// takes 12 parts of the width the others leave, the last, the video file
// name, 22 parts (HikariStatusBar::CalcWidths with widths {-12, 0, ..., -22});
// the fields between are as wide as their text and take no space while
// empty, as an empty legacy field had width 0. Each shown field has legacy's
// tooltip (none on the first) and is a static text for assistive technology
// (its text, then the tooltip as its description); the strip is the window's
// status bar, so a screen reader's "read status bar" command reads it. Like
// legacy's (AcceptsFocus false) it takes no keyboard focus.
//
// Between the first field and the video fields, the rewrite's selection count
// (G1, the accepted announcement policy) and save state (V2), auto-sized like
// legacy's middle fields.
Rectangle {
    id: bar
    objectName: "statusBar"
    required property var shell
    required property var fieldsController // StatusBarController
    // The rewrite's own fields: the selection count and the save state.
    property string selectionText
    property string saveText
    // Legacy WINDOW_WARNING_ELEMENTS on fields 5 and 7 while the subtitles'
    // resolution is not the video's (K2: Theme.warning).
    readonly property color warningColour: "#e0a030"

    // The status text role (visual-language.md: 10 against the 13 body),
    // kept legible on small base fonts.
    readonly property font statusFont: {
        const base = Qt.application.font
        const f = Qt.font({family: base.family})
        if (base.pointSizeF > 0)
            f.pointSizeF = Math.max(base.pointSizeF * 10 / 13, 8)
        else
            f.pixelSize = Math.max(Math.round(base.pixelSize * 10 / 13), 11)
        return f
    }

    implicitHeight: row.implicitHeight + 14
    color: palette.window
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Status bar")

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: bar.palette.mid
    }

    component Field: Label {
        id: field
        // Legacy's tooltip; the field's accessible description.
        property string tip
        // 0: as wide as the text (hidden while empty); otherwise it takes a
        // share of the width the other fields leave (its
        // Layout.horizontalStretchFactor, which only a filling field may set).
        property int stretch: 0
        property bool warning: false
        visible: stretch > 0 || text.length > 0
        font: bar.statusFont
        color: warning ? bar.warningColour : bar.palette.windowText
        elide: Text.ElideRight
        maximumLineCount: 1
        verticalAlignment: Text.AlignVCenter
        Layout.fillHeight: true
        Layout.fillWidth: stretch > 0
        Layout.preferredWidth: stretch > 0 ? 0 : implicitWidth
        Layout.minimumWidth: stretch > 0 ? 0 : implicitWidth
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: tip
        Accessible.ignored: text.length === 0
        HoverHandler { id: hover }
        // Legacy shows a field's tooltip only while it has text.
        ToolTip.visible: hover.hovered && tip.length > 0 && text.length > 0
        ToolTip.text: tip
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 7
        anchors.bottomMargin: 7
        spacing: 14

        // 0: help and progress text, never the editing target (the
        // Document's tab and the window title name it).
        Field {
            objectName: "statusText"
            text: bar.shell.statusText
            stretch: 12
            Layout.horizontalStretchFactor: 12
        }
        Field {
            objectName: "selectionStatus"
            text: bar.selectionText
        }
        Field {
            objectName: "saveStatus"
            text: bar.saveText
        }
        Repeater {
            // Fields 1 to 7.
            model: [
                {name: "statusVideoScale", field: 1},
                {name: "statusVideoZoom", field: 2},
                {name: "statusVideoDuration", field: 3},
                {name: "statusFramesPerSecond", field: 4},
                {name: "statusVideoResolution", field: 5},
                {name: "statusAspectRatio", field: 6},
                {name: "statusSubtitlesResolution", field: 7}
            ]
            delegate: Field {
                required property var modelData
                objectName: modelData.name
                text: bar.fieldsController.fields[modelData.field] ?? ""
                tip: bar.fieldsController.tooltips[modelData.field] ?? ""
                warning: (modelData.field === 5 || modelData.field === 7) && bar.fieldsController.resolutionMismatch
            }
        }
        // 8: the video file name.
        Field {
            objectName: "statusVideoName"
            text: bar.fieldsController.fields[8] ?? ""
            tip: bar.fieldsController.tooltips[8] ?? ""
            stretch: 22
            Layout.horizontalStretchFactor: 22
        }
    }
}
