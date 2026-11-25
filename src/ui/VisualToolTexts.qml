pragma Translator: "HikariSub.VisualTools"
import QtQml
import Hikari.Ui

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

    function freehandHint() {
        //: The hint of the vector clip and drawing tools, after their own; Freehand is a mode button
        return qsTr("Freehand: hold and draw.")
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
        case 7: return qsTr("Click to add points, drag them to reshape. Only what is inside shows.") + " " + freehandHint()
        case 8: return qsTr("Click to add points, drag them to reshape. Or pick a shape and drag a box.") + " " + freehandHint()
        case 9: return qsTr("Drag to move the positions, clips and origins of the picked lines together. Shift locks an axis.")
        case 10: return qsTr("Choose a tag, then drag sideways on the picture to change it. Shift for small steps.")
        case 11: return qsTr("Drag the corners to lay the text on a surface. Drag inside to move it.")
        case 12: return qsTr("Drag across the text to set the gradient’s direction. Drag the ends to adjust; Enter applies.")
        case 13: return qsTr("Click to add path points, drag them to shape the path. The text follows; Enter applies.")
        }
        return ""
    }

    readonly property var guideIds: ["pointer", "position", "move", "scale", "rotate", "tilt", "rectangleClip",
                                     "vectorClip", "drawing", "shiftPositions", "allTags", "perspective", "gradient",
                                     "textOnPath"]
    function guideId(family) {
        return guideIds[family] ? "Video." + guideIds[family] : ""
    }

    function writesCode(family) {
        return ["\\pos(x,y)", "\\pos(x,y)", "\\move(x1,y1,x2,y2,t1,t2)", "\\fscx \\fscy", "\\frz \\org(x,y)",
                "\\frx \\fry \\org(x,y)", "\\clip(x1,y1,x2,y2) \\iclip(x1,y1,x2,y2)", "\\clip(m … l …)",
                "{\\p1}m … l …", "", "", "\\pos \\org \\frx \\fry \\frz \\fax \\fscx \\fscy", "", ""][family] ?? ""
    }
    function writesNote(family) {
        switch (family) {
        //: The visual tool guide: what the Position pointer writes
        case 0: return qsTr("Ctrl-click writes it.")
        //: The visual tool guide: what the Position shifter writes
        case 9: return qsTr("Moves the positions, move points, clips, drawings and origins already in the lines.")
        //: The visual tool guide: what the All tags tool writes
        case 10: return qsTr("Any tag from its list.")
        //: The visual tool guide: what Perspective writes besides its tags
        case 11: return qsTr("And the border and shadow sizes.")
        //: The visual tool guide: what the Gradient tool writes
        case 12: return qsTr("Copies of the line in clip strips, a mask drawing, or tags for each character or line.")
        //: The visual tool guide: what Text on path writes
        case 13: return qsTr("A line for each letter, with its own position and rotation, along the path.")
        }
        return ""
    }

    function guideLines(family) {
        switch (family) {
        case 0: return [qsTr("A cross follows the pointer and shows its script coordinates."),
                        qsTr("Ctrl-click or middle-click puts the line there.")]
        case 1: return [qsTr("Drag anywhere on the picture to move the line; you need not grab it."),
                        qsTr("Shift locks the drag to one axis."),
                        qsTr("A right-click puts the line at the pointer."),
                        qsTr("Set position by rectangle places the line by a rectangle you draw.")]
        case 2: return [qsTr("The left button sets the start point, the right button the end point."),
                        qsTr("Start and End take their times in milliseconds from the line’s start."),
                        qsTr("Set movement using 2 points and the video position builds the move from a point on two frames.")]
        case 3: return [qsTr("Left-drag scales the width, right-drag the height."),
                        qsTr("Shift-drag or middle-drag scales both."),
                        qsTr("Set scale by rectangle scales the line to fit a rectangle you draw.")]
        case 4: return [qsTr("Drag round the origin cross to turn the line."),
                        qsTr("Drag the cross itself to move the origin."),
                        qsTr("Set angle from 2 points takes the angle from two points you click along something in the picture.")]
        case 5: return [qsTr("Left-drag turns the text round the vertical axis."),
                        qsTr("Right-drag turns it round the horizontal axis; middle-drag does both."),
                        qsTr("Drag the origin cross to move the origin.")]
        case 6: return [qsTr("Drag a rectangle on the picture."),
                        qsTr("Drag its edges or corners to resize it, or inside it to move it."),
                        qsTr("Invert clip switches between showing what is inside and what is outside.")]
        case 7:
        case 8: return [qsTr("Choose a mode: move points, add a line, a Bézier curve or a B-spline, draw freehand, add a separate point or delete one."),
                        qsTr("Click to add points and drag them to reshape. Right-click a segment to curve, straighten, split or delete it."),
                        qsTr("Ctrl-click selects several points. Alt while dragging snaps to another point’s x or y."),
                        qsTr("In Freehand, hold the button and draw. Smoothing sets how closely the curves follow the stroke."),
                        family === 7 ? qsTr("Invert clip switches between showing what is inside and what is outside.")
                                     : qsTr("Or pick a shape and drag a box to draw it. Edit shapes… adds your own.")]
        case 9: return [qsTr("Drag to move everything the Move toggles choose, by the same amount in every picked line."),
                        qsTr("Shift locks the drag to one axis."),
                        qsTr("Or type an amount in X and Y: each Enter shifts the lines by that much.")]
        case 10: return [qsTr("Choose a tag in the inspector."),
                         qsTr("Drag sideways on the picture, or turn the wheel, to change its value. Shift for small steps."),
                         qsTr("Set, Add, Multiply and Gradient decide how the value goes across characters or lines.")]
        case 11: return [qsTr("Drag a square corner to lay the text on a surface."),
                         qsTr("Drag inside the text to move it."),
                         qsTr("While dragging a corner, Shift keeps it on an edge, Ctrl snaps it and Alt sizes about the centre."),
                         qsTr("A double-click brings the nearest corner to the pointer.")]
        case 12: return [qsTr("Choose a mode, then drag across the sign from where the gradient starts to where it ends."),
                         qsTr("Drag the ends and the other handles to adjust it; the preview follows."),
                         qsTr("Enter applies the gradient as one step.")]
        case 13: return [qsTr("Give the line a vector clip, or click on the picture to draw a path."),
                         qsTr("Drag the points to shape the path; the text follows it."),
                         qsTr("Enter applies: each letter becomes its own line, and the original is kept as a comment.")]
        }
        return []
    }

    function guideKeys(family, toolKey) {
        const k = (keys, text) => ({ keys: keys, text: text })
        const out = []
        if (toolKey.length > 0)
            //: The visual tool guide's keys: what the tool's own key does
            out.push(k(toolKey, qsTr("Chooses this tool")))
        if ([1, 2, 3, 6, 7, 8, 9, 11].includes(family)) {
            //: The visual tool guide's keys; the arrows work while the pointer is over the picture
            out.push(k(qsTr("Arrow keys"), family === 3 ? qsTr("Change the scale by 1 %")
                                         : family === 7 || family === 8 ? qsTr("Move the selected points by one pixel")
                                         : qsTr("Nudge by one pixel")))
            if (family !== 3)
                out.push(k(qsTr("Shift+Arrow keys"), qsTr("Nudge by a tenth of a pixel")))
        }
        if (family === 2)
            out.push(k(qsTr("Alt+Arrow keys"), qsTr("Nudge the end point")))
        if (family === 7 || family === 8)
            out.push(k(qsTr("Wheel"), qsTr("Steps through the modes")))
        if (family === 10)
            out.push(k(qsTr("Wheel"), qsTr("Changes the value")))
        if (family === 12 || family === 13) {
            out.push(k(ShellMenus.keyText("Enter"), qsTr("Applies")))
            out.push(k(ShellMenus.keyText("Esc"), qsTr("Drops the preview")))
        }
        return out
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
                        h(qsTr("Origin"), "cross"), h(qsTr("Clip"), "outline"), h(qsTr("Drawing"), "outline")]
        case 10: return [h(qsTr("Changed line"), "outline")]
        case 11: return [h(qsTr("Corners"), "square"), h(qsTr("Plane corners"), "circle"), h(qsTr("Origin"), "cross")]
        case 12: return [h(qsTr("Start and end"), "square"), h(qsTr("Skew"), "square"), h(qsTr("P1, P2 and P3"), "square")]
        }
        return []
    }
}
