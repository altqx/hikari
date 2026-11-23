local hikari, ui, listen = ...
local error, pairs, type = error, pairs, type

local function needId(what, id)
    if type(id) ~= "string" or id == "" then
        error(what .. " needs an id", 3)
    end
end

local function answered(result, err)
    if result == nil then
        error(err.message, 3)
    end
    return result
end

hikari.panels = {
    render = function(panel, tree)
        needId("hikari.panels.render", panel)
        answered(ui("panels.render", { panel = panel, tree = tree }))
    end,
    show = function(panel)
        needId("hikari.panels.show", panel)
        answered(ui("panels.show", { panel = panel }))
    end,
    on = function(panel, fn)
        needId("hikari.panels.on", panel)
        listen("panel:" .. panel, fn)
    end,
    post = function(panel, data)
        needId("hikari.panels.post", panel)
        return answered(ui("panels.post", { panel = panel, data = data })).delivered
    end,
}

hikari.dialogs = {
    show = function(dialog, spec)
        needId("hikari.dialogs.show", dialog)
        answered(ui("dialogs.show", { dialog = dialog, spec = spec }))
    end,
    ask = function(dialog, spec)
        needId("hikari.dialogs.ask", dialog)
        local answer = answered(ui("dialogs.ask", { dialog = dialog, spec = spec }))
        return answer.button, answer.values
    end,
    close = function(dialog)
        needId("hikari.dialogs.close", dialog)
        answered(ui("dialogs.close", { dialog = dialog }))
    end,
    on = function(dialog, fn)
        needId("hikari.dialogs.on", dialog)
        listen("dialog:" .. dialog, fn)
    end,
}

local function needSpec(what, spec)
    if type(spec) ~= "table" then
        error(what .. " needs a table", 3)
    end
end

local function withId(key, id, fields)
    local params = { [key] = id }
    for k, v in pairs(fields) do
        params[k] = v
    end
    return params
end

local function asked(name, spec)
    local answer = answered(ui(name, spec))
    if not answer.cancelled then
        return answer
    end
    if answer.back then
        return nil, "back"
    end
    if answer.button then
        return nil, "button", answer.button
    end
    return nil
end

local function live(name, key)
    return {
        open = function(spec)
            spec = spec or {}
            needSpec("hikari." .. name .. ".open", spec)
            return answered(ui(name .. ".open", spec))[key]
        end,
        update = function(id, fields)
            needId("hikari." .. name .. ".update", id)
            needSpec("hikari." .. name .. ".update", fields)
            answered(ui(name .. ".update", withId(key, id, fields)))
        end,
        close = function(id)
            needId("hikari." .. name .. ".close", id)
            answered(ui(name .. ".close", { [key] = id }))
        end,
        on = function(id, fn)
            needId("hikari." .. name .. ".on", id)
            listen(name .. ":" .. id, fn)
        end,
    }
end

hikari.quick_pick = live("quick_pick", "pick")
hikari.quick_pick.ask = function(spec)
    needSpec("hikari.quick_pick.ask", spec)
    local answer, why, button = asked("quick_pick.ask", spec)
    if answer then
        return answer.items
    end
    return nil, why, button
end

hikari.input_box = live("input_box", "box")
hikari.input_box.ask = function(spec)
    spec = spec or {}
    needSpec("hikari.input_box.ask", spec)
    local answer, why = asked("input_box.ask", spec)
    if answer then
        return answer.value
    end
    return nil, why
end
