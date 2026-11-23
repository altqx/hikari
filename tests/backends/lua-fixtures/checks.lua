script_name = "Checks"

aegisub.register_macro("One line", "", function() end, function(subs, sel)
    return #sel == 1, "Select one line"
end)

aegisub.register_macro("Toggle", "", function() end, nil, function(subs, sel, active)
    return active == sel[1]
end)

aegisub.register_macro("Forever", "", function() end, function()
    local n = 0
    while true do n = n + 1 end
end)

aegisub.register_macro("Raises", "", function() end, function()
    error("validation broke")
end, function()
    error("isactive broke")
end)

aegisub.register_macro("Plain", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "plain ran"
    subs[sel[1]] = l
end)

aegisub.register_macro("Shows a dialog", "", function() end, function()
    aegisub.dialog.display({ { class = "label", label = "Go on?", x = 0, y = 0 } })
    return true
end)

aegisub.register_macro("Edits", "", function() end, function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited while checking"
    subs[sel[1]] = l
    return true
end)
