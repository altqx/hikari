-- S3: the legacy automation capture probe. drive.py binds each macro to a
-- hotkey in the legacy app's own Config/Hotkeys.txt ("Script <path>-<k>"),
-- presses it under Xvfb, drives any dialog by keyboard and reads one JSON
-- line per macro run from $HIKARI_CAPTURE_OUT. The macro order is fixed:
-- the dialog cases first, the corpus capture last (scripts it loads may
-- register macros, which would otherwise move the ordinals).
--
-- What this observes is the legacy Lua environment as the app provides it:
-- its aegisub API, include path, native modules, FFI and DependencyControl.
-- Corpus scripts are loaded in this probe's state, one after another in the
-- real global table (cleared of the probe's script_* and restored after
-- each); package.loaded is shared between them (recorded in the output).
-- The hosts remove loadfile/dofile, so scripts are read and compiled with
-- loadstring under their own path.

script_name = "Hikari capture probe"
script_description = "Legacy automation capture (S3)"
script_author = "hikari"
script_version = "1"

-- The settings come from the environment (drive.py). A desktop launch
-- cannot set it (drive_windows.py), so when a setting is unset it is read
-- from capture-probe.cfg next to this script: KEY=VALUE lines.
local cfg
local function setting(key)
    local v = os.getenv(key)
    if v ~= nil then return v end
    if cfg == nil then
        cfg = {}
        local source = debug.getinfo(1, "S").source:gsub("^@", "")
        local dir = source:match("^(.*)[/\\][^/\\]*$")
        local f = dir and io.open(dir .. "/capture-probe.cfg", "r")
        if f then
            for line in f:lines() do
                local k, val = line:gsub("\r$", ""):match("^%s*([%w_]+)%s*=(.*)$")
                if k then cfg[k] = val end
            end
            f:close()
        end
    end
    return cfg[key]
end

local function out(record)
    local path = setting("HIKARI_CAPTURE_OUT")
    local f = assert(io.open(path, "a"))
    f:write(record, "\n")
    f:close()
end

-- A small JSON writer: numbers as %.17g, nil as null, tables with only
-- 1..n keys as arrays, other tables as objects with sorted keys.
local json
local function str(s)
    return '"' .. s:gsub('[%c"\\]', function(c)
        local map = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
        return map[c] or string.format("\\u%04x", c:byte())
    end) .. '"'
end
json = function(v)
    local t = type(v)
    if v == nil then return "null"
    elseif t == "boolean" then return tostring(v)
    elseif t == "number" then
        if v ~= v or v == math.huge or v == -math.huge then return str(tostring(v)) end
        return string.format("%.17g", v)
    elseif t == "string" then return str(v)
    elseif t == "table" then
        local n = #v
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        if #keys == n then
            local parts = {}
            for i = 1, n do parts[i] = json(v[i]) end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local parts = {}
        for _, k in ipairs(keys) do parts[#parts + 1] = str(tostring(k)) .. ":" .. json(v[k]) end
        return "{" .. table.concat(parts, ",") .. "}"
    end
    return str("<" .. t .. ">")
end

-- Each value with its Lua type, so false/nil and number/string stay apart.
local function typed(v)
    return { type = type(v), value = (type(v) == "number" or type(v) == "string" or type(v) == "boolean") and v or nil }
end

-- The dialog subset (L2 / A33-compat): drive.py (drive_windows.py) sends the keys named in
-- plan.json for each case, in this order.
local function all_controls()
    return {
        { class = "label", x = 0, y = 0, label = "Label" },
        { class = "edit", name = "edit", x = 0, y = 1, text = "abc", hint = "edit hint" },
        { class = "textbox", name = "textbox", x = 0, y = 2, text = "line1\nline2" },
        { class = "intedit", name = "int", x = 0, y = 3, value = 5, min = 0, max = 10 },
        { class = "intedit", name = "int_over", x = 1, y = 3, value = 15, min = 0, max = 10 },
        { class = "intedit", name = "int_fraction", x = 2, y = 3, value = 2.7 },
        { class = "floatedit", name = "float", x = 0, y = 4, value = 1.5, min = 0, max = 10, step = 0.25 },
        { class = "floatedit", name = "float_minmax_swapped", x = 1, y = 4, value = 3, min = 10, max = 0 },
        { class = "dropdown", name = "drop", x = 0, y = 5, items = { "a", "b" }, value = "b" },
        { class = "dropdown", name = "drop_missing", x = 1, y = 5, items = { "a", "b" }, value = "zzz" },
        { class = "checkbox", name = "check_unset", x = 0, y = 6, label = "unset" },
        { class = "checkbox", name = "check_on", x = 1, y = 6, label = "on", value = true },
        { class = "color", name = "color_html", x = 0, y = 7, value = "#FF8000" },
        { class = "color", name = "color_ass", x = 1, y = 7, value = "&H0080FF&" },
        { class = "coloralpha", name = "coloralpha", x = 0, y = 8, value = "#FF800040" },
        { class = "alpha", name = "alpha", x = 1, y = 8, value = "&H40&" },
    }
end

local cases = {
    { name = "defaults-ok", controls = all_controls },
    { name = "defaults-escape", controls = all_controls },
    { name = "buttons-return", controls = function() return { { class = "label", label = "b" } } end,
      buttons = { "Apply", "Do it", "Close" }, ids = { close = "Close" } },
    { name = "buttons-escape", controls = function() return { { class = "label", label = "b" } } end,
      buttons = { "Apply", "Do it", "Close" }, ids = { close = "Close" } },
    { name = "button-id-unknown", controls = function() return { { class = "label", label = "b" } } end,
      buttons = { "A", "B" }, ids = { weird = "A" } },
    { name = "button-id-invalid", controls = function() return { { class = "label", label = "b" } } end,
      buttons = { "A" }, ids = { ok = "Nope" } },
    { name = "float-step-up", controls = function() return { { class = "floatedit", name = "f", value = 1, step = 0.5 } } end },
    { name = "int-step-up", controls = function() return { { class = "intedit", name = "i", value = 1 } } end },
    { name = "edit-typed", controls = function() return { { class = "edit", name = "e", text = "" } } end },
}

for index, case in ipairs(cases) do
    aegisub.register_macro("Capture " .. index .. " " .. case.name, case.name, function(subs, sel, active)
        local controls = case.controls()
        local ok, button, values = pcall(aegisub.dialog.display, controls, case.buttons, case.ids)
        local rec = { case = case.name, ok = ok }
        if not ok then
            rec.error = tostring(button)
        else
            rec.button = typed(button)
            rec.values = {}
            for _, c in ipairs(controls) do
                if c.name then rec.values[c.name] = typed(values[c.name]) end
            end
            -- Anything the result has beyond the named controls.
            rec.extra = {}
            for k, v in pairs(values) do
                local known = false
                for _, c in ipairs(controls) do known = known or c.name == k end
                if not known then rec.extra[tostring(k)] = typed(v) end
            end
        end
        out(json(rec))
    end)
end

-- The corpus (L6 / A33-compat): HIKARI_CAPTURE_CORPUS lists one script path per line.
local modules = { "aegisub.re", "aegisub.unicode", "aegisub.lfs", "lfs", "lpeg", "luabins", "ffi", "bit",
                  "aegisub.util", "aegisub.clipboard", "karaskel", "utils", "unicode", "re", "clipboard",
                  "moonscript", "l0.DependencyControl", "json" }

-- Macro runs (L6 third-party corpus): with HIKARI_CAPTURE_RUNS naming a runs
-- file (tests/fixtures/automation-thirdparty/runs.json) and a subtitles
-- object (the corpus macro, not the load-time capture), each listed macro
-- runs right after its script loads, on the probe's own document, with the
-- selection given as event ordinals (Dialogue and Comment lines, from 1).
-- aegisub.dialog.display is answered by the probe from the run's entry, so
-- no dialog is shown (legacy would deadlock: A33-dialog-deadlock), and
-- aegisub.log is recorded instead of shown. The run's document is recorded
-- and the document restored before the next run.
local function load_runs()
    local path = setting("HIKARI_CAPTURE_RUNS")
    if path == nil or path == "" then return {} end
    local f = assert(io.open(path, "rb"))
    local text = f:read("*a")
    f:close()
    local by_script = {}
    for _, run in ipairs(require("json").decode(text).runs) do
        by_script[run.script] = by_script[run.script] or {}
        table.insert(by_script[run.script], run)
    end
    return by_script
end

local function events_of(subs)
    local idx = {}
    for i = 1, #subs do
        if subs[i].class == "dialogue" then idx[#idx + 1] = i end
    end
    return idx
end

local function dump_document(subs)
    local events, header = {}, {}
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            events[#events + 1] = { comment = l.comment, layer = l.layer, start_time = l.start_time,
                end_time = l.end_time, style = l.style, actor = l.actor, margin_l = l.margin_l,
                margin_r = l.margin_r, margin_t = l.margin_t, effect = l.effect, text = l.text }
        elseif l.class == "style" then
            header[#header + 1] = "style " .. tostring(l.name)
        elseif l.class == "info" then
            header[#header + 1] = "info " .. tostring(l.key) .. "=" .. tostring(l.value)
        end
    end
    return { events = events, header = header }
end

-- The answer to a dialog: each named control's initial value, then the
-- run's values; the button by label (true/false for the default OK/Cancel).
local function answer_dialog(run, record)
    local n = 0
    return function(controls, buttons, ids)
        n = n + 1
        local request = { buttons = buttons or {}, controls = {} }
        for _, c in ipairs(controls) do
            request.controls[#request.controls + 1] = tostring(c.class) .. ":" .. tostring(c.name or "")
        end
        record.dialogs[#record.dialogs + 1] = request
        local answer = run.dialogs and run.dialogs[n]
        if not answer then error("probe: no answer for dialog " .. n) end
        local values = {}
        for _, c in ipairs(controls) do
            if c.name then
                local class = tostring(c.class):lower()
                local v
                if class == "edit" or class == "textbox" then v = c.text or c.value or ""
                elseif class == "checkbox" then v = c.value and true or false
                elseif class == "intedit" or class == "floatedit" then v = tonumber(c.value) or 0
                else v = c.value or c.text or "" end
                values[c.name] = v
            end
        end
        for k, v in pairs(answer.values or {}) do values[k] = v end
        local button = answer.button
        if buttons == nil or #buttons == 0 then button = (button == "OK") end
        return button, values
    end
end

local function run_macro(subs, run, procs)
    local record = { macro = run.macro, selection = run.selection, active = run.active, dialogs = {}, log = {} }
    local proc = procs[run.macro]
    if not proc then
        record.ok = false
        record.error = "probe: the script registered no macro named " .. run.macro
        return record
    end
    local saved = {}
    for i = 1, #subs do saved[i] = subs[i] end
    local first = events_of(subs)
    local sel = {}
    for _, o in ipairs(run.selection) do sel[#sel + 1] = first[o] end
    local real_display, real_log = aegisub.dialog.display, aegisub.log
    aegisub.dialog.display = answer_dialog(run, record)
    aegisub.log = function(...)
        local parts = {}
        for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
        record.log[#record.log + 1] = table.concat(parts, "|")
    end
    local ok, a, b = xpcall(function() return proc(subs, sel, first[run.active]) end, debug.traceback)
    aegisub.dialog.display, aegisub.log = real_display, real_log
    record.ok = ok
    if not ok then
        record.error = tostring(a):match("[^\n]*")
    else
        local after = events_of(subs)
        local base = (after[1] or (#subs + 1)) - 1
        if type(a) == "table" then
            record.returned_selection = {}
            for _, i in ipairs(a) do record.returned_selection[#record.returned_selection + 1] = i - base end
        end
        if type(b) == "number" then record.returned_active = b - base end
    end
    record.document = dump_document(subs)
    -- Back to the document as it was: the events are deleted and appended
    -- again in order (the header is left as the run left it).
    local now = events_of(subs)
    if #now > 0 then subs.deleterange(now[1], #subs) end
    for i = first[1] or (#saved + 1), #saved do subs.append(saved[i]) end
    return record
end

local function capture_corpus(subs)
    local runs = subs and load_runs() or {}
    local result = { case = "corpus", lua_version = _VERSION, jit = jit and jit.version or nil,
                     package_path = package.path, package_cpath = package.cpath, modules = {}, scripts = {},
                     shared_package_loaded = true }
    -- The LuaJIT build's Lua 5.2 extensions (LUAJIT_ENABLE_LUA52COMPAT: the
    -- legacy Windows build has them, the Linux package's distribution LuaJIT
    -- does not) and string.buffer (LUAJIT_DISABLE_BUFFER on legacy Windows).
    local ipairs_meta = false
    pcall(function()
        for _ in ipairs(setmetatable({}, { __ipairs = function() ipairs_meta = true; return function() end end })) do end
    end)
    result.lua_features = { table_pack = type(table.pack), ipairs_metamethod = ipairs_meta,
                            break_anywhere = loadstring("repeat break; local x = 1 until true") ~= nil,
                            string_buffer = (pcall(require, "string.buffer")) }
    local api = {}
    for k, v in pairs(aegisub) do api[#api + 1] = k .. ":" .. type(v) end
    table.sort(api)
    result.aegisub_api = api
    for _, m in ipairs(modules) do
        local ok, mod = pcall(require, m)
        result.modules[m] = { ok = ok, type = type(mod), error = (not ok) and tostring(mod):match("[^\n]*") or nil }
    end
    -- C06 stubs as the legacy host provides them.
    result.stubs = {}
    for _, fn in ipairs({ "set_undo_point", "progress", "debug", "cancel" }) do
        result.stubs[fn] = type(aegisub[fn])
    end
    local ok_undo, undo = pcall(function() return aegisub.set_undo_point("probe") end)
    result.stubs.set_undo_point_call = { ok = ok_undo, result = typed(undo) }

    local list = assert(io.open(setting("HIKARI_CAPTURE_CORPUS")))
    local real_macro, real_filter = aegisub.register_macro, aegisub.register_filter
    for path in list:lines() do
        if path ~= "" then
            local rec = { file = path:match("[^/\\]+$"), registrations = {} }
            local procs = {}
            -- Each legacy script has a Lua state of its own; here a script runs
            -- in the real globals (modules such as DependencyControl read
            -- script_* from them), cleared of the probe's own script_* and
            -- restored afterwards.
            local saved = {}
            for k, v in pairs(_G) do saved[k] = v end
            script_name, script_description, script_author, script_version, script_namespace = nil, nil, nil, nil, nil
            aegisub.register_macro = function(name, desc, proc, valid, active)
                if type(name) == "string" then procs[name] = proc end
                rec.registrations[#rec.registrations + 1] = { kind = "macro", name = typed(name), description = typed(desc),
                    processing = type(proc), validation = type(valid), is_active = type(active) }
            end
            aegisub.register_filter = function(name, desc, prio, proc, cfg)
                rec.registrations[#rec.registrations + 1] = { kind = "filter", name = typed(name), description = typed(desc),
                    priority = typed(prio), processing = type(proc), configuration = type(cfg) }
            end
            local chunk, err
            if path:match("%.moon$") then
                local okm, ms = pcall(require, "moonscript.base")
                if okm then chunk, err = ms.loadfile(path) else chunk, err = nil, "moonscript unavailable: " .. tostring(ms) end
            else
                -- The host removes loadfile (legacy replaces it with include):
                -- read the file and compile it under its own name, BOM skipped.
                local f, openErr = io.open(path, "rb")
                if not f then
                    chunk, err = nil, openErr
                else
                    local text = f:read("*a")
                    f:close()
                    if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end
                    chunk, err = loadstring(text, "@" .. path)
                end
            end
            if not chunk then
                rec.loaded = false
                rec.error = tostring(err)
            else
                local ok, e = xpcall(chunk, debug.traceback)
                rec.loaded = ok
                rec.error = (not ok) and tostring(e) or nil
            end
            rec.script_name = typed(rawget(_G, "script_name"))
            rec.script_description = typed(rawget(_G, "script_description"))
            rec.script_author = typed(rawget(_G, "script_author"))
            rec.script_version = typed(rawget(_G, "script_version"))
            rec.script_namespace = typed(rawget(_G, "script_namespace"))
            -- The script's macros run in its globals, before they are restored.
            if runs[rec.file] then
                rec.runs = {}
                for _, run in ipairs(runs[rec.file]) do
                    if rec.loaded then
                        rec.runs[#rec.runs + 1] = run_macro(subs, run, procs)
                    else
                        rec.runs[#rec.runs + 1] = { macro = run.macro, ok = false, error = "probe: the script did not load" }
                    end
                end
            end
            for k in pairs(_G) do
                if saved[k] == nil then rawset(_G, k, nil) end
            end
            for k, v in pairs(saved) do rawset(_G, k, v) end
            result.scripts[#result.scripts + 1] = rec
        end
    end
    list:close()
    aegisub.register_macro, aegisub.register_filter = real_macro, real_filter
    out(json(result))
end

aegisub.register_macro("Capture " .. (#cases + 1) .. " corpus", "corpus", function(subs)
    local ok, err = pcall(capture_corpus, subs)
    if not ok then out(json({ case = "corpus", ok = false, error = tostring(err) })) end
end)

-- In Autoload with HIKARI_CAPTURE_AT_LOAD set, the corpus capture also runs
-- while the host loads this script, so it needs no hotkey.
if setting("HIKARI_CAPTURE_AT_LOAD") then
    local ok, err = pcall(capture_corpus)
    if not ok then out(json({ case = "corpus", ok = false, error = tostring(err), at_load = true })) end
end
