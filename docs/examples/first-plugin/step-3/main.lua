local PANEL = "summary"

local function lineCount()
    return hikari.call("documents.get", {}).lineCount
end

local function unbreak()
    local chosen = {}
    for _, id in ipairs(hikari.selection.get().selected) do
        chosen[id] = true
    end
    return hikari.document.edit("Join line breaks", function(doc, change)
        for _, line in ipairs(doc.lines) do
            if chosen[line.id] and line.text:find("\\N", 1, true) then
                change:set_line(line.id, { text = (line.text:gsub("\\N", " ")) })
            end
        end
    end)
end

local function draw()
    hikari.panels.render(PANEL, {
        kind = "column",
        children = {
            { kind = "label", style = "heading", text = lineCount() .. " lines" },
            { kind = "button", id = "unbreak", text = "Join line breaks", style = "primary" },
        },
    })
end

hikari.commands.register("count", function()
    local info = hikari.call("documents.get", {})
    hikari.notify(info.title .. " has " .. info.lineCount .. " lines.")
end)

hikari.commands.register("unbreak", function()
    local done, err = unbreak()
    if not done then
        hikari.notify("Nothing was joined: " .. err.message, "warning")
    elseif not done.changed then
        hikari.notify("The selected lines have no line breaks.")
    end
end)

hikari.panels.on(PANEL, function(event)
    if event.kind == "click" and event.widget == "unbreak" then
        unbreak()
    end
    draw()
end)
hikari.events.on("document.changed", draw)
