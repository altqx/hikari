script_name = "Export filters"

aegisub.register_filter("Shout", "Upper-cases every line", 1500, function(subs, config)
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            l.text = (config.prefix or "") .. l.text:upper() .. (config.loud and "!" or "")
            subs[i] = l
        end
    end
end, function(subs, options)
    local count = 0
    for k in pairs(options) do count = count + 1 end
    return {
        { class = "label", label = "Prefix (" .. #subs .. " lines, " .. count .. " options)", x = 0, y = 0 },
        { class = "edit", name = "prefix", value = "> ", x = 1, y = 0 },
        { class = "checkbox", name = "loud", label = "Loud", value = true, x = 0, y = 1 },
    }
end)

aegisub.register_filter("Drop the first line", nil, -10, function(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then
            subs.delete(i)
            return
        end
    end
end)

aegisub.register_filter("Undo point", "", 0, function()
    aegisub.set_undo_point("In a filter")
end)

aegisub.register_filter("Broken config", "", 0, function() end, function()
    error("no dialog today")
end)

aegisub.register_filter("Edits while configuring", "", 0, function() end, function(subs)
    subs.delete(#subs)
    return {}
end)
