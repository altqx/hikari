pragma Translator: "HikariSub.QuickPick"
import QtCore
import QtQuick
import Hikari.Ui

Item {
    id: quick
    required property var shell
    required property ExplorerModel explorer
    property real top: 8
    readonly property alias popup: pick
    readonly property string mode: parsed.mode
    readonly property var parsed: quick.parse(pick.text)
    property var projectFiles: []
    property string builtMode: ""
    property string foot: ""
    property string emptyText: ""

    readonly property var prefixes: [
        { prefix: ">", mode: "commands", text: qsTr("Run a command") },
        { prefix: ":", mode: "lines", text: qsTr("Go to a line by number") },
        { prefix: "@", mode: "time", text: qsTr("Go to a time or a frame") },
        { prefix: "#", mode: "styles", text: qsTr("Apply a style") },
        { prefix: "view ", mode: "view", text: qsTr("Show a panel or switch the layout") },
        { prefix: "?", mode: "help", text: qsTr("List these searches") }
    ]
    readonly property var kindIcons: ({ folder: "file-folder", subtitles: "file-subtitles", video: "file-video",
                                        audio: "file-audio", keyframes: "file-keyframes", timecodes: "file-timecodes",
                                        fonts: "tag-font", chapters: "chapters", other: "file-other" })
    readonly property var mediaKinds: ["video", "audio", "keyframes", "timecodes"]

    function parse(text) {
        for (const p of quick.prefixes) {
            const at = p.prefix === "view " ? text.slice(0, 5).toLowerCase() === "view " : text.startsWith(p.prefix)
            if (at)
                return { mode: p.mode, prefix: p.prefix, rest: text.slice(p.prefix.length).replace(/^\s+/, "") }
        }
        return { mode: "files", prefix: "", rest: text }
    }

    function open(prefix) {
        if (pick.visible) {
            const rest = quick.parse(pick.text).rest
            pick.text = prefix + rest
            return
        }
        quick.builtMode = ""
        pick.show(prefix)
    }
    function close() {
        pick.cancel()
    }

    function api(operation, params) {
        return quick.shell.app.apiHost.call(operation, params ?? {})
    }
    function timeText(ms) {
        const cs = Math.floor(ms / 10)
        const two = n => String(n).padStart(2, "0")
        return Math.floor(cs / 360000) + ":" + two(Math.floor(cs / 6000) % 60) + ":" + two(Math.floor(cs / 100) % 60)
            + "." + two(cs % 100)
    }
    readonly property string home: {
        let path = decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.HomeLocation)).replace(/^file:\/\//, ""))
        if (/^\/[A-Za-z]:/.test(path))
            path = path.slice(1)
        return quick.explorer.nativePath(path)
    }
    function homeShort(path) {
        return quick.home.length > 1 && path.startsWith(quick.home) ? "~" + path.slice(quick.home.length) : path
    }
    function folderOf(path) {
        const cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return cut > 0 ? path.slice(0, cut) : ""
    }
    function openTag(path) {
        return quick.shell.app.tabOfFile(quick.shell.shellWindow, path) >= 0 ? qsTr("open") : ""
    }

    function refresh() {
        const p = quick.parsed
        const changed = p.mode !== quick.builtMode
        quick.builtMode = p.mode
        pick.model.keepOrder = p.mode === "help"
        pick.model.filtering = p.mode !== "lines" && p.mode !== "time"
        pick.model.matchOn = p.mode === "files" || p.mode === "commands" ? ["description"] : []
        if (p.mode === "lines" || p.mode === "time") {
            quick.foot = ""
            pick.filterText = ""
            return p.mode === "lines" ? quick.buildLines(p.rest) : quick.buildTime(p.rest)
        }
        pick.filterText = p.rest
        if (!changed)
            return
        quick.foot = ""
        quick.emptyText = p.mode === "files" ? qsTr("No matching files")
                        : p.mode === "commands" ? qsTr("No matching commands") : qsTr("No matches")
        if (p.mode === "files")
            quick.buildFiles()
        else if (p.mode === "commands")
            quick.buildCommands()
        else if (p.mode === "styles")
            quick.buildStyles()
        else if (p.mode === "view")
            quick.buildView()
        else
            pick.model.items = quick.prefixes.map(x => ({ id: x.prefix, label: x.prefix.trim(), description: x.text }))
    }

    function buildFiles() {
        const items = []
        const seen = new Set()
        const recent = quick.shell.app.homeRecent().filter(r => !r.missing).slice(0, 20)
        recent.forEach((r, i) => {
            seen.add(r.path)
            items.push({ id: "recent:" + r.path, label: r.name, description: quick.homeShort(r.folder),
                         icon: "file-subtitles", trailing: quick.openTag(r.path), rank: i,
                         group: qsTr("Recently opened"), path: r.path, recent: true })
        })
        const root = quick.shell.projectRoot
        for (const f of quick.projectFiles) {
            if (seen.has(f.path) || seen.has(quick.explorer.nativePath(f.path)))
                continue
            items.push({ id: "file:" + f.path, label: f.name, description: quick.folderOf(f.relative),
                         icon: quick.kindIcons[f.kind] ?? "file-other", trailing: quick.openTag(f.path),
                         group: qsTr("Project files"), path: f.path, kind: f.kind })
        }
        pick.model.items = items
        if (root.length === 0)
            quick.foot = qsTr("Open a folder as a project to search its files.")
    }

    function rememberCommand(command) {
        const settings = quick.shell.app.settings
        const kept = (settings.value("recent.commands") ?? []).filter(c => c !== command)
        settings.setValue("recent.commands", [command].concat(kept).slice(0, 10))
    }
    function buildCommands() {
        const menus = quick.shell.commandMenus()
        const recent = quick.shell.app.settings.value("recent.commands") ?? []
        const plugins = new Map()
        for (const p of quick.shell.app.pluginRuntime.commands)
            plugins.set(p.id, p)
        const recentItems = []
        const rest = []
        for (const c of quick.shell.app.apiHost.commands()) {
            let description = c.group ?? ""
            let tooltip = ""
            let icon = ""
            if (c.kind === "hotkey") {
                const places = menus[c.command]
                if (places && places.length > 0)
                    description = places[0].split(" > ").slice(0, -1).join(" › ")
            } else if (c.kind === "macro") {
                description = qsTr("Automation")
            } else if (c.kind === "plugin") {
                const p = plugins.get(c.command)
                if (p && p.palette === false)
                    continue
                description = p ? qsTr("Automation") + " › " + p.pluginName : qsTr("Automation")
                tooltip = p?.description ?? ""
                icon = p?.icon ?? ""
            }
            const disabled = c.enabled ? "" : (c.disabledReason ?? "").length > 0 ? c.disabledReason
                                                                                   : qsTr("Not available here")
            const at = recent.indexOf(c.command)
            const item = { id: c.command, label: c.title, description: description, tooltip: tooltip, icon: icon,
                           shortcut: c.kind === "hotkey" ? quick.shell.hotkeys.accelOf(c.command, c.windowIndex) : "",
                           disabled: disabled, commandKind: c.kind, rank: at >= 0 ? at : 1000 }
            if (at >= 0)
                recentItems.push(item)
            else
                rest.push(item)
        }
        recentItems.sort((a, b) => a.rank - b.rank)
        rest.sort((a, b) => a.label.localeCompare(b.label))
        for (const item of recentItems)
            item.group = qsTr("Recently used")
        for (const item of rest)
            item.group = recentItems.length > 0 ? qsTr("All commands") : ""
        pick.model.items = recentItems.concat(rest)
    }
    function runCommand(item) {
        pick.finish()
        quick.rememberCommand(item.id)
        const command = item.id
        const plugin = item.commandKind === "plugin"
        Qt.callLater(() => {
            if (plugin)
                quick.shell.app.pluginRuntime.run(command)
            else
                quick.api("commands.run", { command: command })
        })
    }

    function buildLines(rest) {
        const first = quick.api("lines.list", { limit: 1, fields: ["start", "text"] })
        const total = first?.total ?? 0
        pick.model.items = []
        if (!first) {
            quick.emptyText = qsTr("No matches")
            return
        }
        if (rest.length === 0) {
            quick.emptyText = ""
            quick.foot = qsTr("Type a line number, 1 to %1.").arg(total)
            return
        }
        const n = /^\d+$/.test(rest) ? Number(rest) : NaN
        if (!(n >= 1 && n <= total)) {
            quick.emptyText = qsTr("Not a line: %1. This Document has %n lines.", "", total).arg(rest)
            return
        }
        const line = quick.api("lines.list", { offset: n - 1, limit: 1, fields: ["start", "text"] }).lines[0]
        pick.model.items = [{ id: String(line.id), label: String(n),
                              description: quick.timeText(line.start) + "   " + line.text.replace(/\s+/g, " "),
                              line: line.id }]
    }
    function lineAt(ms) {
        let after = null
        for (let offset = 0;; offset += 5000) {
            const page = quick.api("lines.list", { offset: offset, limit: 5000, fields: ["start", "end"] })
            if (!page || page.lines.length === 0)
                return after
            for (const line of page.lines) {
                if (line.start <= ms && ms < line.end)
                    return line
                if (line.start > ms && (!after || line.start < after.start))
                    after = line
            }
        }
    }
    function buildTime(rest) {
        pick.model.items = []
        quick.emptyText = ""
        if (rest.length === 0)
            return
        const target = quick.shell.video.jumpTarget(rest)
        if (target.kind === undefined) {
            quick.emptyText = qsTr("Not a time or frame: %1. Try 3:40 or a frame number.").arg(rest)
            return
        }
        if (target.frame !== undefined) {
            pick.model.items = [{ id: "frame", label: qsTr("Go to %1 (frame %2)").arg(quick.timeText(target.time)).arg(target.frame),
                                  frame: target.frame }]
            return
        }
        if (target.kind !== "time") {
            quick.emptyText = qsTr("Frames need a video.")
            return
        }
        const line = quick.lineAt(target.value)
        if (!line) {
            quick.emptyText = qsTr("No line shows at %1 or after it.").arg(quick.timeText(target.value))
            return
        }
        pick.model.items = [{ id: "line", label: qsTr("Select the line at %1").arg(quick.timeText(target.value)),
                              line: line.id }]
    }

    function buildStyles() {
        const list = quick.api("styles.list")?.styles ?? []
        const counts = quick.shell.app.styleLineCounts()
        const selected = quick.api("selection.get")?.selected?.length ?? 0
        pick.model.items = list.map(s => ({ id: s.name, label: s.name,
                                             description: s.fontname + " " + s.fontsize,
                                             trailing: qsTr("%n lines", "", counts[s.name] ?? 0),
                                             disabled: selected === 0 ? qsTr("Needs a selection") : "",
                                             lines: counts[s.name] ?? 0 }))
        quick.updateStyleFoot()
    }
    function updateStyleFoot() {
        if (quick.mode !== "styles")
            return
        const item = pick.model.currentItem()
        if (item.id === undefined) {
            quick.foot = ""
            return
        }
        const selected = quick.api("selection.get")?.selected?.length ?? 0
        quick.foot = qsTr("Enter applies “%1” to %n lines", "", selected).arg(item.id) + " · "
            + qsTr("Shift+Enter selects its %n lines", "", item.lines)
    }

    function buildView() {
        const items = []
        quick.shell.builtInDocks.forEach((dock, i) => items.push({
            id: "panel:" + quick.shell.panelName(dock), label: dock.title, group: qsTr("Panels"), dock: i }))
        for (const row of quick.shell.pluginPanelRows ?? [])
            items.push({ id: "plugin:" + row.dock, label: row.title, description: row.pluginName, group: qsTr("Panels"),
                         pluginDock: row.dock })
        const layouts = quick.shell.workspaceLayout
        for (const name of ["Editing", "Timing", "Translation", "Typesetting", "Karaoke"])
            items.push({ id: "preset:" + name, label: qsTranslate("HikariSub.Workspace", name), group: qsTr("Layouts"),
                         preset: name, disabled: layouts.locked ? quick.shell.lockReason : "" })
        for (const saved of layouts.savedLayouts)
            items.push({ id: "saved:" + saved.name, label: saved.name, group: qsTr("Layouts"), saved: saved.name,
                         disabled: layouts.locked ? quick.shell.lockReason : "" })
        pick.model.items = items
    }

    function accept(item) {
        const mode = quick.mode
        if (mode === "help") {
            pick.text = item.id
            return
        }
        if (mode === "commands")
            return quick.runCommand(item)
        pick.finish()
        const shell = quick.shell
        if (mode === "files") {
            if (item.recent) {
                if (!shell.app.revealRecent(item.path))
                    shell.openSubtitles(item.path)
            } else if (item.kind === "subtitles") {
                shell.openFromExplorer(item.path, true)
            } else if (quick.mediaKinds.includes(item.kind)) {
                shell.openMediaFromExplorer(item.path, item.kind)
            }
        } else if (mode === "lines") {
            quick.api("selection.set", { selected: [item.line], active: item.line })
        } else if (mode === "time") {
            if (item.frame !== undefined) {
                shell.video.pause()
                shell.video.showFrameAt(item.frame)
            } else {
                quick.api("selection.set", { selected: [item.line], active: item.line })
            }
        } else if (mode === "styles") {
            shell.editor.chooseStyle(item.id)
        } else if (mode === "view") {
            if (item.dock !== undefined) {
                shell.showPanel(shell.builtInDocks[item.dock])
                shell.focusPanel(shell.panels[item.dock], Qt.OtherFocusReason)
            } else if (item.pluginDock !== undefined) {
                shell.showPluginPanel(item.pluginDock)
            } else if (item.preset !== undefined) {
                shell.applyPreset(item.preset)
            } else {
                shell.applySavedLayout(item.saved)
            }
        }
    }

    Connections {
        target: quick.explorer
        function onFilesFound(found, done) {
            quick.projectFiles = done ? found : quick.projectFiles.concat(found)
            if (pick.visible && quick.mode === "files") {
                quick.buildFiles()
                pick.filterText = quick.parsed.rest
            }
        }
    }

    QuickPick {
        id: pick
        top: quick.top
        accessibleName: quick.mode === "commands" ? qsTr("Command palette") : qsTr("Quick Open")
        placeholder: quick.mode === "commands" ? qsTr("Type the name of a command")
                                               : qsTr("Search files by name. Type ? for the other searches.")
        foot: quick.foot
        emptyText: quick.emptyText
        busy: quick.explorer.scanning
        filterText: ""
        countText: quick.mode === "commands" ? qsTr("%n commands", "", pick.model.count)
                 : quick.mode === "files" ? qsTr("%n files", "", pick.model.count)
                 : qsTr("%n results", "", pick.model.count)
        onTextChanged: if (visible) quick.refresh()
        onOpened: {
            quick.projectFiles = []
            if (quick.shell.projectRoot.length > 0)
                quick.explorer.scanFiles()
            quick.builtMode = ""
            quick.refresh()
        }
        onTextEdited: (before, after) => {
            const was = quick.parse(before)
            if (was.mode !== "files" && was.rest.length === 0 && after.length < before.length && before.startsWith(after))
                pick.text = ""
        }
        onAccepted: item => quick.accept(item)
        onSecondary: item => {
            if (quick.mode !== "styles" || item.id === undefined)
                return
            pick.finish()
            quick.shell.app.selectLinesWithStyle(item.id)
        }
        Connections {
            target: pick.model
            function onCurrentChanged() { quick.updateStyleFoot() }
        }
    }
}
