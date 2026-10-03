-- S3: the legacy automation capture probe. drive.py binds each macro to a
-- hotkey in the legacy app's own Config/Hotkeys.txt ("Script <path>-<k>"),
-- presses it under Xvfb, drives any dialog by keyboard and reads one JSON
-- line per macro run from $HIKARI_CAPTURE_OUT. The macro order is fixed:
-- the dialog cases first, the corpus capture last (scripts it loads may
-- register macros, which would otherwise move the ordinals).
--
-- What this observes is the legacy Lua environment as the app provides it:
-- its aegisub API, include path, native modules, FFI and DependencyControl.
-- Corpus scripts are loaded in this probe's state, each with its own global
-- table; package.loaded is shared between them (recorded in the output).

script_name = "Hikari capture probe"
script_description = "Legacy automation capture (S3)"
script_author = "hikari"
script_version = "1"

local function out(record)
    local path = os.getenv("HIKARI_CAPTURE_OUT")
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

-- The dialog subset (L2 / A33-compat): drive.py sends the keys named in
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

-- The corpus (L6 / A33-compat): $HIKARI_CAPTURE_CORPUS lists one script path per line.
local modules = { "aegisub.re", "aegisub.unicode", "aegisub.lfs", "lfs", "lpeg", "luabins", "ffi", "bit",
                  "aegisub.util", "aegisub.clipboard", "karaskel", "utils", "unicode", "re", "clipboard",
                  "moonscript", "l0.DependencyControl", "json" }

local function capture_corpus()
    local result = { case = "corpus", lua_version = _VERSION, jit = jit and jit.version or nil,
                     package_path = package.path, package_cpath = package.cpath, modules = {}, scripts = {},
                     shared_package_loaded = true }
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

    local list = assert(io.open(os.getenv("HIKARI_CAPTURE_CORPUS")))
    local real_macro, real_filter = aegisub.register_macro, aegisub.register_filter
    for path in list:lines() do
        if path ~= "" then
            local rec = { file = path:match("[^/\\]+$"), registrations = {} }
            local env = setmetatable({}, { __index = _G })
            env._G = env
            aegisub.register_macro = function(name, desc, proc, valid, active)
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
                chunk, err = loadfile(path)
            end
            if not chunk then
                rec.loaded = false
                rec.error = tostring(err)
            else
                setfenv(chunk, env)
                local ok, e = xpcall(chunk, debug.traceback)
                rec.loaded = ok
                rec.error = (not ok) and tostring(e) or nil
            end
            rec.script_name = typed(rawget(env, "script_name"))
            rec.script_description = typed(rawget(env, "script_description"))
            rec.script_author = typed(rawget(env, "script_author"))
            rec.script_version = typed(rawget(env, "script_version"))
            rec.script_namespace = typed(rawget(env, "script_namespace"))
            result.scripts[#result.scripts + 1] = rec
        end
    end
    list:close()
    aegisub.register_macro, aegisub.register_filter = real_macro, real_filter
    out(json(result))
end

aegisub.register_macro("Capture " .. (#cases + 1) .. " corpus", "corpus", function()
    local ok, err = pcall(capture_corpus)
    if not ok then out(json({ case = "corpus", ok = false, error = tostring(err) })) end
end)
