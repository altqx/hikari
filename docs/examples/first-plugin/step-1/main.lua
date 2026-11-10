hikari.commands.register("count", function()
    local info = hikari.call("documents.get", {})
    hikari.notify(info.title .. " has " .. info.lineCount .. " lines.")
end)
