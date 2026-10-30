pragma Translator: "HikariSub.Video"
import QtQuick

Item {
    id: overlay
    objectName: "videoRenderCost"
    property var cost: ({})
    property real textTop: 8
    enabled: false
    readonly property bool drawn: cost.milliseconds !== undefined

    function milliseconds(ms) {
        return ms < 10 ? ms.toFixed(1) : Math.round(ms).toString()
    }

    Column {
        x: 8
        y: overlay.textTop
        spacing: 2
        Repeater {
            model: {
                const c = overlay.cost
                if (!overlay.drawn)
                    return [qsTr("No subtitles on this frame")]
                //: Over the video: how long drawing the frame's subtitles took
                const lines = [qsTr("Subtitles drawn in %1 ms").arg(overlay.milliseconds(c.milliseconds))]
                if (c.counted) {
                    //: Over the video: %1 events shown, %2 bitmaps libass drew, %3 their area in megapixels
                    lines.push(qsTr("%1 event(s), %2 bitmap(s), %3 Mpx").arg(c.events, c.images,
                                                                                    (c.area / 1e6).toFixed(2)))
                    if (c.blurred > 0 || c.vectorClips > 0)
                        //: Over the video: of the shown events, %1 use \blur or \be and %2 a drawing \clip or \iclip
                        lines.push(qsTr("%1 with blur, %2 with a vector clip").arg(c.blurred, c.vectorClips))
                }
                return lines
            }
            Text {
                required property string modelData
                objectName: "videoRenderCostLine"
                text: modelData
                color: "#ffffff"
                style: Text.Outline
                styleColor: "#000000"
                font.pointSize: Qt.application.font.pointSize + 1
            }
        }
    }
}
