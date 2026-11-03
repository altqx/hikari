script_name = "Border style"

aegisub.register_macro("Rewrite styles", "", function(subs)
    local seen = {}
    for i = 1, #subs do
        local s = subs[i]
        if s.class == "style" then
            seen[s.name] = s.borderstyle
            subs[i] = s
        end
    end
    assert(seen.Outline == 1 and seen.Box == 3 and seen.Lines == 4,
           "read " .. tostring(seen.Outline) .. "," .. tostring(seen.Box) .. "," .. tostring(seen.Lines))
end)
