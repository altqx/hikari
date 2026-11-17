script_name = "Undo points"

local function mark(subs, i, text)
    local l = subs[i]
    l.text = text
    subs[i] = l
end

aegisub.register_macro("Two steps", "", function(subs)
    mark(subs, 4, "first")
    subs.append({ class = "dialogue", section = "[Events]", comment = false, layer = 0, start_time = 7000,
        end_time = 8000, style = "Default", actor = "", margin_l = 0, margin_r = 0, margin_t = 0, effect = "",
        text = "added" })
    aegisub.set_undo_point("First step")
    aegisub.set_undo_point("Nothing new")
    mark(subs, 5, "second")
end)

aegisub.register_macro("Named last", "", function(subs)
    mark(subs, 4, "named")
    aegisub.set_undo_point("Named step")
end)

aegisub.register_macro("Only an undo point", "", function()
    aegisub.set_undo_point("Empty")
end)

aegisub.register_macro("Step then error", "", function(subs)
    mark(subs, 4, "dropped")
    aegisub.set_undo_point("Dropped step")
    error("broke after a step")
end)

aegisub.register_macro("Undo point while validating", "", function() end, function()
    aegisub.set_undo_point("In validation")
    return true
end)
