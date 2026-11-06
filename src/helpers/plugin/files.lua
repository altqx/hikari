local hikari, files, net = ...
local call, error, type = hikari.call, error, type

local function handle(what, file)
    if type(file) == "table" then
        file = file.file
    end
    if type(file) ~= "string" or file == "" then
        error(what .. " needs a file: a handle or a table from hikari.files", 3)
    end
    return file
end

local function inPlace(what, place, path)
    if type(place) ~= "string" or place == "" then
        error(what .. " needs a place: \"data\", \"cache\" or a folder id", 3)
    end
    if path ~= nil and type(path) ~= "string" then
        error(what .. " needs the path as text", 3)
    end
    return { place = place, path = path or "" }
end

local function field(name, result, err)
    if not result then
        return nil, err
    end
    return result[name]
end

hikari.files = {
    pick = function(options)
        if options ~= nil and type(options) ~= "table" then
            error("hikari.files.pick takes a table of options", 2)
        end
        local result, err = files("files.pick", options or {})
        if not result then
            return nil, err
        end
        if result.file == nil then
            return nil
        end
        return result
    end,
    folder = function(id)
        if type(id) ~= "string" or id == "" then
            error("hikari.files.folder needs a folder id", 2)
        end
        return field("granted", files("files.folder", { folder = id }))
    end,
    open = function(place, path)
        return files("files.open", inPlace("hikari.files.open", place, path))
    end,
    entries = function(place, path)
        return field("entries", files("files.entries", inPlace("hikari.files.entries", place, path)))
    end,
    list = function()
        return field("files", call("files.list", {}))
    end,
    read = function(file, encoding)
        return field("data", call("files.read", { file = handle("hikari.files.read", file), encoding = encoding }))
    end,
    write = function(file, data, encoding)
        local target = handle("hikari.files.write", file)
        if type(data) ~= "string" then
            error("hikari.files.write needs the data as a string", 2)
        end
        local result, err = call("files.write", { file = target, data = data, encoding = encoding })
        if not result then
            return nil, err
        end
        return true
    end,
}

hikari.net = {
    fetch = function(request)
        if type(request) == "string" then
            request = { url = request }
        end
        if type(request) ~= "table" or type(request.url) ~= "string" then
            error("hikari.net.fetch needs a url", 2)
        end
        return net("net.fetch", request)
    end,
}
