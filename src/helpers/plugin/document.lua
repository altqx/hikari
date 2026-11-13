local hikari, lock, unlock = ...
local call, array, null = hikari.call, hikari.array, hikari.null
local error, ipairs, pcall, setmetatable, type = error, ipairs, pcall, setmetatable, type

local function target(document)
    return document ~= nil and { document = document } or {}
end

local function lines(document, revision)
    local all = array({})
    local offset = 0
    while true do
        local page, err = call("lines.list", { document = document, offset = offset, limit = 5000 })
        if not page then
            return nil, err
        end
        if page.revision ~= revision then
            return false
        end
        for _, line in ipairs(page.lines) do
            all[#all + 1] = line
        end
        offset = offset + #page.lines
        if offset >= page.total or #page.lines == 0 then
            return all
        end
    end
end

local function snapshot(document)
    for _ = 1, 3 do
        local summary, err = call("documents.get", target(document))
        if not summary then
            return nil, err
        end
        local id, revision = summary.document, summary.revision
        local read
        read, err = lines(id, revision)
        if read == nil then
            return nil, err
        end
        if read then
            local styles, info
            styles, err = call("styles.list", { document = id })
            if not styles then
                return nil, err
            end
            info, err = call("info.list", { document = id })
            if not info then
                return nil, err
            end
            if styles.revision == revision and info.revision == revision then
                return {
                    document = id,
                    revision = revision,
                    title = summary.title,
                    format = summary.format,
                    lines = read,
                    styles = styles.styles,
                    info = info.info,
                }
            end
        end
    end
    return nil, { code = "busy", message = "the Document kept changing while the plugin read it" }
end

local Change = {}
Change.__index = Change

local function stage(change, edit)
    change.edits[#change.edits + 1] = edit
end

function Change:set_line(line, fields)
    stage(self, { op = "set_line", line = line, fields = fields })
end

function Change:insert_line(fields, where)
    local edit = { op = "insert_line", fields = fields }
    if where then
        edit.after, edit.before = where.after, where.before
    end
    stage(self, edit)
end

function Change:delete_line(line)
    stage(self, { op = "delete_line", line = line })
end

function Change:move_line(line, before)
    stage(self, { op = "move_line", line = line, before = before == nil and null or before })
end

function Change:set_style(name, fields)
    stage(self, { op = "set_style", name = name, fields = fields })
end

function Change:delete_style(name)
    stage(self, { op = "delete_style", name = name })
end

function Change:set_info(key, value)
    stage(self, { op = "set_info", key = key, value = value == nil and null or value })
end

local editing = false

local function edit(name, prepare, options)
    if type(name) ~= "string" or name == "" then
        error("hikari.document.edit needs a step name", 2)
    end
    if type(prepare) ~= "function" then
        error("hikari.document.edit needs a function that stages the edits", 2)
    end
    if editing then
        error("hikari.document.edit can't run inside another edit", 2)
    end
    local refused = lock()
    if refused then
        return nil, { code = "busy", message = refused }
    end
    editing = true
    local ok, result, err = pcall(function()
        local doc, readError = snapshot(options and options.document)
        if not doc then
            return nil, readError
        end
        local change = setmetatable({ edits = array({}) }, Change)
        prepare(doc, change)
        if #change.edits == 0 then
            return { revision = doc.revision, inserted = array({}), changed = false }
        end
        return call("document.apply", { document = doc.document, revision = doc.revision, name = name, edits = change.edits })
    end)
    editing = false
    unlock()
    if not ok then
        error(result, 0)
    end
    return result, err
end

local function group(name, steps)
    if type(name) ~= "string" or name == "" then
        error("hikari.document.group needs a step name", 2)
    end
    if type(steps) ~= "function" then
        error("hikari.document.group needs a function that makes the steps", 2)
    end
    local opened, err = call("document.group_begin", { name = name })
    if not opened then
        return nil, err
    end
    local results = { pcall(steps) }
    local ended = call("document.group_end", {})
    if not results[1] then
        error(results[2], 0)
    end
    return ended or { steps = 0 }, unpack(results, 2)
end

hikari.document = {
    list = function()
        local result, err = call("documents.list", {})
        if not result then
            return nil, err
        end
        return result.documents
    end,
    read = snapshot,
    edit = edit,
    group = group,
}

hikari.selection = {
    get = function(document)
        return call("selection.get", target(document))
    end,
    set = function(selected, active, document)
        local params = target(document)
        params.selected = array(selected or {})
        params.active = active
        return call("selection.set", params)
    end,
}

hikari.vcs = {
    refs = function(options)
        if options ~= nil and type(options) ~= "table" then
            error("hikari.vcs.refs takes a table of options", 2)
        end
        return call("vcs.refs", options or {})
    end,
    show = function(ref, document)
        if type(ref) ~= "string" or ref == "" then
            error("hikari.vcs.show needs a ref: a branch, tag or commit", 2)
        end
        local params = target(document)
        params.ref = ref
        local result, err = call("vcs.show", params)
        if not result then
            return nil, err
        end
        return result.text, result
    end,
}
