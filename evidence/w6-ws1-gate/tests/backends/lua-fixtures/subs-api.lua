-- L4 fixture: the staged subtitles object (legacy AutoToFile semantics).
script_name = "Subs API"

aegisub.register_macro("Edit", "", function(subs, sel, active)
    assert(subs[1].class == "info" and subs[1].key == "Title", "info line")
    assert(subs[3].class == "style" and subs[3].name == "Default", "style line")
    assert(subs[4].class == "dialogue" and #subs == 6, "dialogue lines")
    local first = sel[1]
    local l = subs[first]
    l.text = "appended"
    subs.append(l)          -- one two three appended
    l.text = "negative"
    subs[-first] = l        -- negative one two three appended
    subs.delete(first + 2)  -- negative one three appended
    l = subs[first + 1]
    l.text = l.text .. "!"
    subs[first + 1] = l     -- the slot keeps its Line
    return {first}, first
end)

aegisub.register_macro("Out of range", "", function(subs)
    subs[100] = subs[4]
end)

aegisub.register_macro("Wrong class", "", function(subs)
    subs[1] = subs[4]
end)

aegisub.register_macro("Bad field", "", function(subs)
    local l = subs[4]
    l.layer = "high"
    subs[4] = l
end)

aegisub.register_macro("Write", "", function(subs)
    local l = subs[4]
    l.text = "x"
    subs[4] = l
end)
