script_name = "Big file"

aegisub.register_macro("Raw", "", function(subs)
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            aegisub.log(l.raw .. "\n")
        end
    end
end)

aegisub.register_macro("Edit last", "", function(subs)
    local lines = 0
    for i = 1, #subs do
        if subs[i].class == "dialogue" then
            lines = lines + 1
        end
    end
    local l = subs[#subs]
    l.text = "edited " .. lines
    subs[#subs] = l
end)
