-- F1/F3 capture: one macro that writes the open Document's state as one
-- JSON line to $HIKARI_CAPTURE_OUT (capture-probe.cfg beside this script
-- when the environment is unset): every Dialogue/Comment Line (row number
-- among the events, comment flag, style, actor, effect, text), the
-- selected rows, the active row, and the Line editor's selection
-- (aegisub.gui.get_selection, 1-based inclusive start, exclusive end as
-- the host returns them, read from EditBox::GetEditor). It changes
-- nothing and returns nothing, so the host keeps the selection.

script_name = "Hikari state dump"
script_description = "Legacy capture: write the Document state (F1/F3)"
script_author = "hikari"
script_version = "1"

local function setting(key)
    local v = os.getenv(key)
    if v ~= nil then return v end
    local source = debug.getinfo(1, "S").source:gsub("^@", "")
    local dir = source:match("^(.*)[/\\][^/\\]*$")
    local f = dir and io.open(dir .. "/capture-probe.cfg", "r")
    if not f then return nil end
    local found
    for line in f:lines() do
        local k, val = line:gsub("\r$", ""):match("^%s*([%w_]+)%s*=(.*)$")
        if k == key then found = val end
    end
    f:close()
    return found
end

local function str(s)
    return '"' .. tostring(s):gsub('[%c"\\]', function(c)
        local map = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
        return map[c] or string.format("\\u%04x", c:byte())
    end) .. '"'
end

local function dump(subs, sel, active)
    local first
    local rows = {}
    local lines = {}
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            first = first or i
            rows[i] = i - first
            lines[#lines + 1] = string.format('{"row":%d,"comment":%s,"style":%s,"actor":%s,"effect":%s,"text":%s}',
                i - first, tostring(l.comment), str(l.style), str(l.actor), str(l.effect), str(l.text))
        end
    end
    local selected = {}
    for _, i in ipairs(sel) do selected[#selected + 1] = tostring(rows[i] or ("raw" .. i)) end
    local ok, s, e = pcall(function() return aegisub.gui.get_selection() end)
    local okc, c = pcall(function() return aegisub.gui.get_cursor() end)
    local record = string.format('{"lines":[%s],"selected":[%s],"active":%s,"editor_selection":%s,"editor_cursor":%s}',
        table.concat(lines, ","), table.concat(selected, ","), tostring(rows[active] or -1),
        ok and string.format("[%d,%d]", s, e) or str(s), okc and tostring(c) or str(c))
    local f = assert(io.open(setting("HIKARI_CAPTURE_OUT"), "a"))
    f:write(record, "\n")
    f:close()
end

aegisub.register_macro(script_name, script_description, dump)
