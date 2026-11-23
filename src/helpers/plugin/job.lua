local hikari, job = ...
local call, array, error, type = hikari.call, hikari.array, error, type

local function refused(result, err)
    if not result then
        return nil, err
    end
    return true
end

hikari.job = {
    begin = function(options)
        if type(options) ~= "table" or type(options.title) ~= "string" or options.title == "" then
            error("hikari.job.begin needs {title = text}", 2)
        end
        return refused(job("job.begin", { title = options.title }))
    end,
    progress = function(fraction, text)
        if fraction ~= nil and type(fraction) ~= "number" then
            error("hikari.job.progress takes a fraction from 0 to 1, or nil while it can't tell", 2)
        end
        if text ~= nil and type(text) ~= "string" then
            error("hikari.job.progress takes the text as its second argument", 2)
        end
        return refused(job("job.progress", { fraction = fraction, text = text }))
    end,
    cancelled = function()
        local result = job("job.cancelled", {})
        return result ~= nil and result.cancelled == true
    end,
}

hikari.marks = {
    set = function(marks, document)
        if type(marks) ~= "table" then
            error("hikari.marks.set takes a list of {line, colour, tooltip}", 2)
        end
        return call("marks.set", { marks = array(marks), document = document })
    end,
    clear = function(lines, document)
        return call("marks.clear", { lines = lines and array(lines) or nil, document = document })
    end,
    list = function(document)
        local result, err = call("marks.list", { document = document })
        if not result then
            return nil, err
        end
        return result.marks
    end,
}

local function options(what, given)
    if given ~= nil and type(given) ~= "table" then
        error(what .. " takes a table of options", 3)
    end
    return given or {}
end

hikari.problems = {
    set = function(problems, given)
        if type(problems) ~= "table" then
            error("hikari.problems.set takes a list of {line, severity, message}", 2)
        end
        local o = options("hikari.problems.set", given)
        return call("problems.set", { problems = array(problems), revision = o.revision, set = o.set, document = o.document })
    end,
    clear = function(given)
        local o = options("hikari.problems.clear", given)
        local result, err = call("problems.clear", { document = o.document, set = o.set })
        if not result then
            return nil, err
        end
        return result.cleared
    end,
    list = function(given)
        local o = options("hikari.problems.list", given)
        local result, err = call("problems.list", {
            document = o.document,
            lines = o.lines and array(o.lines) or nil,
            severity = o.severity,
            source = o.source,
        })
        if not result then
            return nil, err
        end
        return result.problems
    end,
    fix = function(problem, fix, document)
        return call("problems.fix", { problem = problem, fix = fix, document = document })
    end,
}

hikari.status = {
    set = function(item, fields)
        if type(item) ~= "string" or (fields ~= nil and type(fields) ~= "table") then
            error("hikari.status.set takes a status item id and {text, icon, tooltip, tone, visible}", 2)
        end
        fields = fields or {}
        return refused(call("status_items.set", { item = item, text = fields.text, icon = fields.icon,
            tooltip = fields.tooltip, tone = fields.tone, visible = fields.visible }))
    end,
}

local function programCall(what, program, options)
    if type(program) ~= "string" or program == "" then
        error(what .. " needs the id of a program the manifest declares", 3)
    end
    if options ~= nil and type(options) ~= "table" then
        error(what .. " takes a table of options", 3)
    end
    options = options or {}
    local handles
    if options.files ~= nil then
        handles = {}
        for i, file in ipairs(options.files) do
            handles[i] = type(file) == "table" and file.file or file
        end
        handles = array(handles)
    end
    return {
        program = program,
        args = options.args and array(options.args) or nil,
        files = handles,
        stdin = options.stdin,
        encoding = options.encoding,
        timeout = options.timeout,
    }
end

local function process(what, p)
    if type(p) ~= "string" or p == "" then
        error(what .. " needs a process from hikari.programs.start", 3)
    end
    return p
end

hikari.programs = {
    run = function(program, options)
        return call("programs.run", programCall("hikari.programs.run", program, options))
    end,
    start = function(program, options)
        local params = programCall("hikari.programs.start", program, options)
        if params.stdin ~= nil or params.timeout ~= nil then
            error("hikari.programs.start takes no stdin or timeout; send to the process instead", 2)
        end
        local result, err = call("programs.start", params)
        if not result then
            return nil, err
        end
        return result.process
    end,
    send = function(p, data, close)
        if data ~= nil and type(data) ~= "string" then
            error("hikari.programs.send takes the data as text", 2)
        end
        return refused(call("programs.send", { process = process("hikari.programs.send", p), data = data, close = close == true or nil }))
    end,
    stop = function(p)
        return refused(call("programs.stop", { process = process("hikari.programs.stop", p) }))
    end,
}
