local hikari, ui, listen = ...
local error, type = error, type

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
