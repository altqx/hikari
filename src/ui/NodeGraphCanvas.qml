pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: root
    objectName: "nodeGraphPanel"
    required property NodeEffectsController controller
    padding: 0
    implicitWidth: 640
    implicitHeight: 480
    readonly property size minimumSize: Qt.size(320, 240)
    readonly property Item toolbar: headerBar

    readonly property int nodeWidth: 156
    readonly property int headerHeight: 30
    readonly property int rowHeight: 20
    readonly property int gridStep: 20
    property real zoom: 1
    property real panX: 40
    property real panY: 40
    property var positions: ({})
    property var unfolded: ({})
    property var pending: null
    property var target: null
    property string refusal: ""
    property real fittedFor: -1

    readonly property bool editable: controller.editable && !controller.newerFormat

    function nodeById(id) {
        for (const n of controller.nodes)
            if (n.id === id)
                return n
        return null
    }
    function luaRows(n) {
        return n.lua ? 1 : 0
    }
    function foldable(n) {
        return n.folds && n.outputs.some(o => !o.wired)
    }
    function shownOutputs(n) {
        return n.folds && !unfolded[n.id] ? n.outputs.filter(o => o.wired) : n.outputs
    }
    function rowCount(n) {
        return n.fields.length + luaRows(n) + n.inputs.length + shownOutputs(n).length + (foldable(n) ? 1 : 0)
    }
    function nodeHeight(n) {
        return headerHeight + rowCount(n) * rowHeight + 6
    }
    function position(n) {
        return positions[n.id] ?? Qt.point(n.x, n.y)
    }
    function inputPoint(n, name) {
        const p = position(n)
        const i = Math.max(0, n.inputs.findIndex(x => x.name === name))
        return Qt.point(p.x, p.y + headerHeight + (n.fields.length + luaRows(n) + i + 0.5) * rowHeight)
    }
    function outputPoint(n, name) {
        const p = position(n)
        const outs = shownOutputs(n)
        let i = outs.findIndex(x => x.name === name)
        if (i < 0)
            i = outs.length
        return Qt.point(p.x + nodeWidth, p.y + headerHeight + (n.fields.length + luaRows(n) + n.inputs.length + i + 0.5) * rowHeight)
    }
    function toWorld(point) {
        return Qt.point((point.x - panX) / zoom, (point.y - panY) / zoom)
    }
    function inputAt(world) {
        for (const n of controller.nodes) {
            const p = position(n)
            if (world.x < p.x - 10 || world.x > p.x + nodeWidth || world.y < p.y || world.y > p.y + nodeHeight(n))
                continue
            const row = Math.floor((world.y - p.y - headerHeight) / rowHeight) - n.fields.length - luaRows(n)
            return { node: n.id, port: row >= 0 && row < n.inputs.length ? n.inputs[row].name : "" }
        }
        return null
    }
    function portColour(type) {
        return Theme.ports[type] ?? Theme.muted
    }
    function announcement(n) {
        const wired = n.inputs.filter(p => p.wired).length
        //: A node on the canvas for a screen reader: "Choose, palette: 1 of 1 inputs wired"
        return qsTr("%1, %2: %3 of %4 inputs wired").arg(n.title, n.id).arg(wired).arg(n.inputs.length)
    }

    function select(id, add) {
        if (!add) {
            controller.selection = id === "" ? [] : [id]
            return
        }
        const chosen = controller.selection.slice()
        const at = chosen.indexOf(id)
        if (at >= 0)
            chosen.splice(at, 1)
        else
            chosen.push(id)
        controller.selection = chosen
    }
    function centre(n) {
        const p = position(n)
        return Qt.point(p.x + nodeWidth / 2, p.y + nodeHeight(n) / 2)
    }
    function centreOn(id) {
        const n = nodeById(id)
        if (!n)
            return
        const c = centre(n)
        panX = viewport.width / 2 - c.x * zoom
        panY = viewport.height / 2 - c.y * zoom
    }
    function ensureVisible(n) {
        const p = position(n)
        const left = p.x * zoom + panX, top = p.y * zoom + panY
        const right = left + nodeWidth * zoom, bottom = top + nodeHeight(n) * zoom
        if (left < 0 || top < 0 || right > viewport.width || bottom > viewport.height)
            centreOn(n.id)
    }
    function step(dx, dy) {
        const nodes = controller.nodes
        if (nodes.length === 0)
            return
        const current = nodeById(controller.selected)
        if (!current) {
            select(nodes[0].id, false)
            ensureVisible(nodes[0])
            return
        }
        const from = centre(current)
        let best = null, bestScore = Infinity
        for (const n of nodes) {
            if (n.id === current.id)
                continue
            const c = centre(n)
            const along = (c.x - from.x) * dx + (c.y - from.y) * dy
            if (along <= 0)
                continue
            const score = along + 2 * Math.abs((c.x - from.x) * dy - (c.y - from.y) * dx)
            if (score < bestScore) {
                bestScore = score
                best = n
            }
        }
        if (best) {
            select(best.id, false)
            ensureVisible(best)
        }
    }
    function nudge(dx, dy) {
        const n = nodeById(controller.selected)
        if (!n || !editable)
            return
        const p = position(n)
        controller.moveNode(n.id, p.x + dx * gridStep, p.y + dy * gridStep)
    }
    function zoomAt(point, to) {
        const z = Math.min(2, Math.max(0.25, to))
        const world = toWorld(point)
        zoom = z
        panX = point.x - world.x * z
        panY = point.y - world.y * z
    }
    function fit() {
        const nodes = controller.nodes
        if (nodes.length === 0 || viewport.width <= 0) {
            zoom = 1
            panX = 40
            panY = 40
            return
        }
        let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity
        for (const n of nodes) {
            const p = position(n)
            left = Math.min(left, p.x)
            top = Math.min(top, p.y)
            right = Math.max(right, p.x + nodeWidth)
            bottom = Math.max(bottom, p.y + nodeHeight(n))
        }
        const z = Math.min(1, Math.max(0.25, Math.min((viewport.width - 60) / (right - left),
                                                      (viewport.height - 60) / (bottom - top))))
        zoom = z
        panX = (viewport.width - (right - left) * z) / 2 - left * z
        panY = (viewport.height - (bottom - top) * z) / 2 - top * z
    }

    function startWire(node, port, type, at) {
        pending = { fromNode: node, fromPort: port, type: type, x: 0, y: 0, detachNode: "", detachPort: "" }
        dragWire(at)
    }
    function pickUpWire(node, port, at) {
        const w = controller.wires.find(x => x.toNode === node && x.toPort === port)
        if (!w)
            return false
        pending = { fromNode: w.fromNode, fromPort: w.fromPort, type: w.type, x: 0, y: 0, detachNode: node, detachPort: port }
        dragWire(at)
        return true
    }
    function dragWire(at) {
        if (!pending)
            return
        const world = toWorld(at)
        const hit = inputAt(world)
        target = hit && hit.port !== ""
                 ? { node: hit.node, port: hit.port,
                     problem: hit.node === pending.detachNode && hit.port === pending.detachPort ? ""
                              : controller.wireProblem(pending.fromNode, pending.fromPort, hit.node, hit.port) }
                 : null
        pending = Object.assign({}, pending, { x: world.x, y: world.y, at: at })
    }
    function dropWire(at) {
        const p = pending
        if (!p)
            return
        const hit = inputAt(toWorld(at))
        pending = null
        target = null
        if (hit && hit.port !== "") {
            if (hit.node === p.detachNode && hit.port === p.detachPort)
                return
            const problem = controller.wireProblem(p.fromNode, p.fromPort, hit.node, hit.port)
            if (problem !== "") {
                showRefusal(problem)
                return
            }
            if (p.detachNode !== "")
                controller.disconnectInput(p.detachNode, p.detachPort)
            showRefusal(controller.connectPorts(p.fromNode, p.fromPort, hit.node, hit.port))
        } else if (!hit) {
            if (p.detachNode !== "")
                controller.disconnectInput(p.detachNode, p.detachPort)
            else
                addPopup.openAt(at, p, "")
        }
    }
    function showRefusal(text) {
        refusal = text
        if (text !== "")
            refusalTimer.restart()
    }
    function addAt(type) {
        const at = addPopup.at ?? Qt.point(viewport.width / 2, viewport.height / 2)
        const world = toWorld(at)
        const from = addPopup.from
        const id = controller.addNode(type, Math.round(world.x), Math.round(world.y - (from ? headerHeight + rowHeight / 2 : 0)),
                                      from ? from.fromNode : "", from ? from.fromPort : "")
        viewport.forceActiveFocus()
        return id
    }

    Timer {
        id: refusalTimer
        interval: 3000
        onTriggered: root.refusal = ""
    }

    Connections {
        target: root.controller
        function onGraphChanged() {
            root.positions = ({})
            wires.requestPaint()
            if (root.fittedFor !== root.controller.current + root.controller.path.length * 1e-3) {
                root.fittedFor = root.controller.current + root.controller.path.length * 1e-3
                Qt.callLater(root.fit)
            }
        }
        function onSelectionChanged() { wires.requestPaint() }
    }
    onZoomChanged: { grid.requestPaint(); wires.requestPaint() }
    onPanXChanged: { grid.requestPaint(); wires.requestPaint() }
    onPanYChanged: { grid.requestPaint(); wires.requestPaint() }
    onPendingChanged: wires.requestPaint()
    Connections {
        target: Theme
        function onChanged() { grid.requestPaint(); wires.requestPaint() }
    }

    RowLayout {
        id: headerBar
        objectName: "nodeGraphHeader"
        visible: parent !== root.contentItem
        spacing: 2
        Repeater {
            model: root.controller.path.length > 0 ? [""].concat(root.controller.path) : []
            delegate: RowLayout {
                id: crumb
                required property string modelData
                required property int index
                spacing: 2
                Label {
                    visible: crumb.index > 0
                    text: "›"
                    color: Theme.muted
                }
                ToolButton {
                    objectName: "nodeGraphCrumb_" + crumb.index
                    text: crumb.index === 0 ? root.controller.name : crumb.modelData
                    enabled: crumb.index < root.controller.path.length
                    onClicked: {
                        while (root.controller.path.length > crumb.index)
                            root.controller.leaveGroup()
                    }
                }
            }
        }
        IconToolButton {
            objectName: "nodeGraphAdd"
            iconRole: "add"
            text: qsTr("Add node")
            display: AbstractButton.TextBesideIcon
            enabled: root.controller.hasGraph && root.editable
            onClicked: addPopup.openAt(null, null, "")
        }
        Label {
            objectName: "nodeGraphCount"
            visible: root.controller.hasGraph
            Layout.leftMargin: 6
            color: Theme.muted
            //: The graph's size in the Graph header: "12 nodes · 13 wires"
            text: (root.controller.nodes.length === 1 ? qsTr("1 node") : qsTr("%n nodes", "", root.controller.nodes.length))
                  + " · " + (root.controller.wires.length === 1 ? qsTr("1 wire") : qsTr("%n wires", "", root.controller.wires.length))
        }
        IconToolButton {
            objectName: "nodeGraphFit"
            iconRole: "zoom-reset"
            text: qsTr("Fit the graph in view")
            enabled: root.controller.hasGraph
            onClicked: root.fit()
        }
        Label {
            objectName: "nodeGraphZoom"
            visible: root.controller.hasGraph
            color: Theme.muted
            //: The canvas zoom: "100 %"
            text: qsTr("%1 %").arg(Math.round(root.zoom * 100))
        }
    }

    Item {
        id: viewport
        objectName: "nodeCanvas"
        anchors.fill: parent
        clip: true
        visible: root.controller.hasGraph
        focus: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Graph")
        onWidthChanged: if (root.fittedFor >= 0 && width > 0 && root.zoom === 1 && root.panX === 40) root.fit()

        Rectangle {
            anchors.fill: parent
            color: Theme.field
        }
        Canvas {
            id: grid
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const step = root.gridStep * root.zoom
                if (step < 6)
                    return
                ctx.fillStyle = Theme.line
                const size = Math.max(1, Math.round(root.zoom * 1.5))
                for (let x = ((root.panX % step) + step) % step; x < width; x += step)
                    for (let y = ((root.panY % step) + step) % step; y < height; y += step)
                        ctx.fillRect(Math.round(x), Math.round(y), size, size)
            }
        }
        MouseArea {
            id: background
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            property point last
            property bool panned: false
            onPressed: mouse => {
                viewport.forceActiveFocus()
                last = Qt.point(mouse.x, mouse.y)
                panned = false
            }
            onPositionChanged: mouse => {
                root.panX += mouse.x - last.x
                root.panY += mouse.y - last.y
                panned = panned || Math.abs(mouse.x - last.x) + Math.abs(mouse.y - last.y) > 0
                last = Qt.point(mouse.x, mouse.y)
            }
            onClicked: mouse => {
                if (!panned && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                    root.select("", false)
            }
            onDoubleClicked: mouse => {
                if (root.editable)
                    addPopup.openAt(Qt.point(mouse.x, mouse.y), null, "")
            }
            onWheel: wheel => root.zoomAt(Qt.point(wheel.x, wheel.y), root.zoom * Math.pow(1.0015, wheel.angleDelta.y))
        }
        Canvas {
            id: wires
            anchors.fill: parent
            function draw(ctx, a, b, colour, width) {
                const dx = Math.max(30, Math.abs(b.x - a.x) / 2)
                ctx.beginPath()
                ctx.moveTo(a.x, a.y)
                ctx.bezierCurveTo(a.x + dx, a.y, b.x - dx, b.y, b.x, b.y)
                ctx.strokeStyle = colour
                ctx.lineWidth = width
                ctx.stroke()
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.setTransform(root.zoom, 0, 0, root.zoom, root.panX, root.panY)
                const nodes = {}
                for (const n of root.controller.nodes)
                    nodes[n.id] = n
                const chosen = root.controller.selection
                const p = root.pending
                for (const w of root.controller.wires) {
                    const a = nodes[w.fromNode], b = nodes[w.toNode]
                    if (!a || !b || (p && p.detachNode === w.toNode && p.detachPort === w.toPort))
                        continue
                    const thick = chosen.indexOf(w.fromNode) >= 0 || chosen.indexOf(w.toNode) >= 0
                    draw(ctx, root.outputPoint(a, w.fromPort), root.inputPoint(b, w.toPort), root.portColour(w.type), thick ? 3 : 1.5)
                }
                if (p && nodes[p.fromNode]) {
                    const refused = root.target && root.target.problem !== ""
                    draw(ctx, root.outputPoint(nodes[p.fromNode], p.fromPort), Qt.point(p.x, p.y),
                         refused ? Theme.danger : root.portColour(p.type), 2)
                }
            }
        }

        Item {
            id: world
            x: root.panX
            y: root.panY
            scale: root.zoom
            transformOrigin: Item.TopLeft

            Repeater {
                model: root.controller.nodes
                delegate: Item {
                    id: card
                    required property var modelData
                    readonly property var node: modelData
                    readonly property bool chosen: root.controller.selection.indexOf(node.id) >= 0
                    objectName: "node_" + node.id
                    x: node.x
                    y: node.y
                    width: root.nodeWidth
                    height: root.nodeHeight(node)
                    onXChanged: moved()
                    onYChanged: moved()
                    function moved() {
                        if (x === node.x && y === node.y)
                            return
                        const next = Object.assign({}, root.positions)
                        next[node.id] = Qt.point(x, y)
                        root.positions = next
                        wires.requestPaint()
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: root.announcement(node)
                    Accessible.selected: chosen
                    Accessible.description: node.problem ? qsTr("Has a problem") : ""

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: Theme.raised
                        border.width: card.chosen || card.node.problem ? 2 : 1
                        border.color: !card.node.known ? "transparent"
                                      : card.node.problem ? Theme.danger : card.chosen ? Theme.accent : Theme.line
                    }
                    Canvas {
                        objectName: "nodeUnknown_" + card.node.id
                        visible: !card.node.known
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.setLineDash([4, 3])
                            ctx.lineWidth = 2
                            ctx.strokeStyle = Theme.danger
                            ctx.strokeRect(1, 1, width - 2, height - 2)
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        drag.target: root.editable ? card : null
                        drag.threshold: 4
                        property bool dragged: false
                        onPressed: mouse => {
                            viewport.forceActiveFocus()
                            dragged = false
                            if (!card.chosen || mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                                root.select(card.node.id, mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                        }
                        onPositionChanged: dragged = drag.active
                        onReleased: {
                            if (dragged)
                                root.controller.moveNode(card.node.id, Math.round(card.x), Math.round(card.y))
                        }
                        onDoubleClicked: if (card.node.group) root.controller.enterGroup(card.node.id)
                    }
                    Column {
                        x: 8
                        y: 4
                        width: parent.width - 16
                        Label {
                            width: parent.width
                            text: card.node.title !== "" ? card.node.title : card.node.type
                            font.bold: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        Label {
                            width: parent.width
                            text: card.node.id
                            font.pixelSize: 11
                            color: Theme.muted
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                    Icon {
                        visible: card.node.problem
                        x: parent.width - width - 6
                        y: 6
                        iconRole: "problem"
                        size: 14
                        color: Theme.danger
                        accentColor: Theme.danger
                    }
                    Column {
                        y: root.headerHeight
                        width: parent.width
                        Repeater {
                            model: card.node.fields
                            delegate: Label {
                                required property var modelData
                                x: 8
                                width: root.nodeWidth - 16
                                height: root.rowHeight
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                textFormat: Text.StyledText
                                text: Qt.escapeHtml(modelData.name.charAt(0).toUpperCase() + modelData.name.slice(1))
                                      + " <font color=\"" + Theme.muted + "\">" + Qt.escapeHtml(modelData.text) + "</font>"
                            }
                        }
                        Label {
                            visible: card.node.lua
                            x: 8
                            width: root.nodeWidth - 16
                            height: visible ? root.rowHeight : 0
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            font.family: "monospace"
                            color: root.controller.trusted ? Theme.muted : Theme.warning
                            text: root.controller.trusted ? card.node.code.split("\n")[0] : qsTr("Not trusted")
                        }
                        Repeater {
                            model: card.node.inputs
                            delegate: Item {
                                id: inputRow
                                required property var modelData
                                readonly property bool refused: root.pending !== null
                                    && !(root.pending.detachNode === card.node.id && root.pending.detachPort === modelData.name)
                                    && root.controller.wireProblem(root.pending.fromNode, root.pending.fromPort, card.node.id, modelData.name) !== ""
                                objectName: "nodeInput_" + card.node.id + "_" + modelData.name
                                width: root.nodeWidth
                                height: root.rowHeight
                                opacity: refused ? 0.35 : 1
                                NodePort {
                                    x: -width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    type: inputRow.modelData.type
                                    hollow: !inputRow.modelData.wired && inputRow.modelData.constant !== ""
                                }
                                Label {
                                    x: 10
                                    width: parent.width - 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    textFormat: Text.StyledText
                                    text: Qt.escapeHtml(inputRow.modelData.name)
                                          + (inputRow.modelData.constant !== ""
                                             ? " <font color=\"" + Theme.muted + "\">" + Qt.escapeHtml(inputRow.modelData.constant) + "</font>" : "")
                                }
                                MouseArea {
                                    x: -8
                                    width: 18
                                    height: parent.height
                                    enabled: root.editable && inputRow.modelData.wired
                                    cursorShape: enabled ? Qt.CrossCursor : Qt.ArrowCursor
                                    preventStealing: true
                                    onPressed: mouse => {
                                        if (!root.pickUpWire(card.node.id, inputRow.modelData.name, mapToItem(viewport, mouse.x, mouse.y)))
                                            mouse.accepted = false
                                    }
                                    onPositionChanged: mouse => root.dragWire(mapToItem(viewport, mouse.x, mouse.y))
                                    onReleased: mouse => root.dropWire(mapToItem(viewport, mouse.x, mouse.y))
                                }
                            }
                        }
                        Repeater {
                            model: root.shownOutputs(card.node)
                            delegate: Item {
                                id: outputRow
                                required property var modelData
                                objectName: "nodeOutput_" + card.node.id + "_" + modelData.name
                                width: root.nodeWidth
                                height: root.rowHeight
                                Label {
                                    x: 8
                                    width: parent.width - 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                    textFormat: Text.PlainText
                                    text: outputRow.modelData.name
                                }
                                NodePort {
                                    x: parent.width - width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    type: outputRow.modelData.type
                                }
                                MouseArea {
                                    x: parent.width - 10
                                    width: 18
                                    height: parent.height
                                    enabled: root.editable
                                    cursorShape: Qt.CrossCursor
                                    preventStealing: true
                                    onPressed: mouse => root.startWire(card.node.id, outputRow.modelData.name, outputRow.modelData.type,
                                                                       mapToItem(viewport, mouse.x, mouse.y))
                                    onPositionChanged: mouse => root.dragWire(mapToItem(viewport, mouse.x, mouse.y))
                                    onReleased: mouse => root.dropWire(mapToItem(viewport, mouse.x, mouse.y))
                                }
                            }
                        }
                        Label {
                            id: foldRow
                            readonly property int hidden: card.node.outputs.length - root.shownOutputs(card.node).length
                            objectName: "nodeFold_" + card.node.id
                            visible: root.foldable(card.node)
                            x: 8
                            width: root.nodeWidth - 16
                            height: visible ? root.rowHeight : 0
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.accent
                            text: hidden > 0 ? (hidden === 1 ? qsTr("1 more output") : qsTr("%n more outputs", "", hidden))
                                             : qsTr("Fewer outputs")
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const next = Object.assign({}, root.unfolded)
                                    next[card.node.id] = !next[card.node.id]
                                    root.unfolded = next
                                    wires.requestPaint()
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "nodeWireRefusal"
            readonly property string message: root.target && root.target.problem !== "" ? root.target.problem : root.refusal
            readonly property point at: root.pending && root.pending.at ? root.pending.at : Qt.point(viewport.width / 2, viewport.height - 40)
            visible: message !== ""
            x: Math.min(viewport.width - width - 8, Math.max(8, at.x + 14))
            y: Math.min(viewport.height - height - 8, Math.max(8, at.y + 14))
            width: Math.min(320, refusalText.implicitWidth + 16)
            height: refusalText.implicitHeight + 10
            radius: 3
            color: Theme.panel
            border.width: 1
            border.color: Theme.danger
            Label {
                id: refusalText
                objectName: "nodeWireRefusalText"
                x: 8
                y: 5
                width: parent.width - 16
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: parent.message
            }
        }

        Keys.onPressed: event => {
            const ctrl = event.modifiers & Qt.ControlModifier
            const dirs = { [Qt.Key_Left]: [-1, 0], [Qt.Key_Right]: [1, 0], [Qt.Key_Up]: [0, -1], [Qt.Key_Down]: [0, 1] }
            if (dirs[event.key] !== undefined) {
                const d = dirs[event.key]
                if (ctrl)
                    root.nudge(d[0], d[1])
                else
                    root.step(d[0], d[1])
            } else if (event.key === Qt.Key_Delete && root.editable) {
                root.controller.removeSelected()
            } else if (event.key === Qt.Key_G && ctrl && root.editable) {
                root.controller.groupSelection()
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                const n = root.nodeById(root.controller.selected)
                if (n && n.group)
                    root.controller.enterGroup(n.id)
            } else if (event.key === Qt.Key_Backspace) {
                root.controller.leaveGroup()
            } else if (event.key === Qt.Key_Escape && root.pending) {
                root.pending = null
                root.target = null
            } else if (!ctrl && root.editable && event.text.length === 1 && /\S/.test(event.text)) {
                addPopup.openAt(null, null, event.text)
            } else {
                return
            }
            event.accepted = true
        }
    }

    NodeAddPopup {
        id: addPopup
        controller: root.controller
        parent: viewport
        onChosen: type => root.addAt(type)
    }

    ColumnLayout {
        objectName: "nodeGraphEmpty"
        visible: !root.controller.hasGraph && root.controller.available
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 10
        Icon {
            Layout.alignment: Qt.AlignHCenter
            iconRole: "node-editor"
            size: 32
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.bold: true
            wrapMode: Text.Wrap
            text: root.controller.unreadable !== "" ? qsTr("This graph can’t be read") : qsTr("This file has no node effects yet")
        }
        Label {
            objectName: "nodeGraphEmptyText"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.muted
            textFormat: Text.PlainText
            text: root.controller.unreadable !== "" ? root.controller.unreadable
                  : qsTr("A graph turns karaoke lines into new lines. It is saved in the file as a Comment line, and the lines it writes are marked fx with its name.")
        }
        RowLayout {
            visible: root.controller.unreadable === ""
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            PrimaryButton {
                objectName: "nodeGraphNew"
                text: qsTr("New graph")
                icon.source: IconTheme.image("add", Theme.onAccent, Theme.onAccent, false)
                icon.color: "transparent"
                enabled: root.editable
                onClicked: root.controller.newGraph()
            }
            Button {
                id: presetButton
                objectName: "nodeGraphFromPreset"
                text: qsTr("From a preset")
                enabled: root.editable
                down: presetMenu.visible
                onClicked: presetMenu.popup(presetButton, 0, presetButton.height)
                Accessible.role: Accessible.ButtonMenu
            }
            Button {
                id: libraryButton
                objectName: "nodeGraphFromLibrary"
                text: qsTr("From the library…")
                icon.source: IconTheme.image("node-library", enabled ? IconTheme.normal : IconTheme.disabled,
                                             enabled ? IconTheme.accent : IconTheme.disabled, false)
                icon.color: "transparent"
                enabled: root.editable && root.controller.library.length > 0
                down: libraryMenu.visible
                onClicked: libraryMenu.popup(libraryButton, 0, libraryButton.height)
                Accessible.role: Accessible.ButtonMenu
            }
        }
    }

    NodePresetMenu {
        id: presetMenu
        controller: root.controller
    }
    NodeLibraryMenu {
        id: libraryMenu
        controller: root.controller
    }
}
