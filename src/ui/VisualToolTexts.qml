pragma Translator: "HikariSub.VisualTools"
import QtQml

QtObject {
    // MUST KILL: families are application::visual::Family's magic ints
    readonly property var groups: [[0], [1, 2, 3, 4, 5, 11], [6, 7], [8, 12, 13], [9, 10]]

    function shortName(family) {
        switch (family) {
        //: A visual tool's short name in the full-screen tool rail
        case 0: return qsTr("Pointer")
        //: A visual tool's short name in the full-screen tool rail
        case 1: return qsTr("Position")
        //: A visual tool's short name in the full-screen tool rail
        case 2: return qsTr("Move")
        //: A visual tool's short name in the full-screen tool rail
        case 3: return qsTr("Scale")
        //: A visual tool's short name in the full-screen tool rail
        case 4: return qsTr("Rotate")
        //: A visual tool's short name in the full-screen tool rail
        case 5: return qsTr("Tilt X / Y")
        //: A visual tool's short name in the full-screen tool rail
        case 6: return qsTr("Rectangle clip")
        //: A visual tool's short name in the full-screen tool rail
        case 7: return qsTr("Vector clip")
        //: A visual tool's short name in the full-screen tool rail
        case 8: return qsTr("Drawing")
        //: A visual tool's short name in the full-screen tool rail
        case 9: return qsTr("Shift positions")
        //: A visual tool's short name in the full-screen tool rail
        case 10: return qsTr("All tags")
        //: A visual tool's short name in the full-screen tool rail
        case 11: return qsTr("Perspective")
        //: A visual tool's short name in the full-screen tool rail
        case 12: return qsTr("Gradient")
        //: A visual tool's short name in the full-screen tool rail
        case 13: return qsTr("Text on path")
        }
        return ""
    }

    function hint(family) {
        switch (family) {
        case 0: return qsTr("Point at the picture to read a position. Ctrl-click puts the line there.")
        case 1: return qsTr("Drag to place the line. Shift-drag locks to one axis. Right-click places it at the pointer.")
        case 2: return qsTr("Drag the start or end point; right-drag sets the end. The line travels between them.")
        case 3: return qsTr("Drag to stretch across, right-drag to stretch up. Shift-drag scales both.")
        case 4: return qsTr("Drag round the origin to turn the text. Drag the origin to move it.")
        case 5: return qsTr("Drag sideways to turn round Y, right-drag up or down to turn round X. Drag the origin to move it.")
        case 6: return qsTr("Drag a box. Only what is inside shows. Drag its edges to resize, inside to move.")
        case 7: return qsTr("Click to add points, drag them to reshape. Only what is inside shows.")
        case 8: return qsTr("Click to add points, drag them to reshape. Or pick a shape and drag a box.")
        case 9: return qsTr("Drag to move the positions, clips and origins of the picked lines together. Shift locks an axis.")
        case 10: return qsTr("Choose a tag, then drag sideways on the picture to change it. Shift for small steps.")
        case 11: return qsTr("Drag the corners to lay the text on a surface. Drag inside to move it.")
        case 12: return qsTr("Drag across the text to set the gradient’s direction. Drag the ends to adjust; Enter applies.")
        case 13: return qsTr("Click to add path points, drag them to shape the path. The text follows; Enter applies.")
        }
        return ""
    }

    function markName(mark) {
        switch (mark) {
        //: The shape a visual tool draws for a handle on the frame
        case "square": return qsTr("square")
        //: The shape a visual tool draws for a handle on the frame
        case "circle": return qsTr("circle")
        //: The shape a visual tool draws for a handle on the frame
        case "cross": return qsTr("cross")
        //: The shape a visual tool draws for a handle on the frame
        case "line": return qsTr("line")
        //: The shape a visual tool draws for a handle on the frame
        case "arrow": return qsTr("arrow")
        //: The shape a visual tool draws for a handle on the frame
        case "outline": return qsTr("outline")
        }
        return ""
    }

    function handles(family) {
        const h = (name, mark) => ({ name: name, mark: mark })
        switch (family) {
        case 1: return [h(qsTr("Position"), "square")]
        case 2: return [h(qsTr("Start point"), "square"), h(qsTr("End point"), "circle"), h(qsTr("Path"), "arrow"),
                        h(qsTr("Current position"), "cross")]
        case 3: return [h(qsTr("Width"), "arrow"), h(qsTr("Height"), "arrow"), h(qsTr("Both"), "arrow")]
        case 4: return [h(qsTr("Origin"), "cross"), h(qsTr("Angle"), "line")]
        case 5: return [h(qsTr("Origin"), "cross"), h(qsTr("Tilt grid"), "outline")]
        case 6: return [h(qsTr("Edges and corners"), "outline")]
        case 7:
        case 8:
        case 13: return [h(qsTr("Point"), "square"), h(qsTr("Curve handle"), "circle")]
        case 9: return [h(qsTr("Position and move start"), "square"), h(qsTr("Move end"), "circle"),
                        h(qsTr("Origin"), "cross"), h(qsTr("Clip and drawing"), "outline")]
        case 10: return [h(qsTr("Changed line"), "outline")]
        case 11: return [h(qsTr("Corners"), "square"), h(qsTr("Plane corners"), "circle"), h(qsTr("Origin"), "cross")]
        case 12: return [h(qsTr("Start and end"), "square"), h(qsTr("Skew"), "square"), h(qsTr("P1, P2 and P3"), "square")]
        }
        return []
    }
}
