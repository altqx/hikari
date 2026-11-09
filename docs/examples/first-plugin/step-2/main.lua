local PANEL = "summary"

local function lineCount()
    return hikari.call("documents.get", {}).lineCount
end

local function draw()
    hikari.panels.render(PANEL, {
        kind = "column",
        children = {
            { kind = "label", style = "heading", text = lineCount() .. " lines" },
        },
    })
end

hikari.commands.register("count", function()
    local info = hikari.call("documents.get", {})
    hikari.notify(info.title .. " has " .. info.lineCount .. " lines.")
end)

hikari.panels.on(PANEL, draw)
hikari.events.on("document.changed", draw)
