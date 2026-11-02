script_name = "Extra"

local function first(subs)
    for i = 1, #subs do
        if subs[i].class == "dialogue" then return i end
    end
end

aegisub.register_macro("Set", "", function(subs)
    local i = first(subs)
    local l = subs[i]
    assert(type(l.extra) == "table", "extra is a table")
    assert(l.extra.k == "second", "a later entry wins: " .. tostring(l.extra.k))
    l.extra["hikari.set"] = "a,b:c|d#"
    l.extra.number = 5
    l.extra[1] = "a number key"
    subs[i] = l
    local back = subs[i].extra
    assert(back["hikari.set"] == "a,b:c|d#" and back.number == "5" and back[1] == nil, "read back before saving")
    l = subs[i + 1]
    l.extra = nil
    subs[i + 1] = l
end)

aegisub.register_macro("Check", "", function(subs)
    local l = subs[first(subs)]
    assert(l.extra["hikari.set"] == "a,b:c|d#", "read back after saving")
    assert(l.extra.number == "5" and l.extra.k == "second", "kept values")
end)

aegisub.register_macro("Not a table", "", function(subs)
    local i = first(subs)
    local l = subs[i]
    l.extra = "x"
    subs[i] = l
end)
