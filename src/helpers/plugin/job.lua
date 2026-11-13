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
