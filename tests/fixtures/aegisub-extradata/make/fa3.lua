script_name = "fa3"
script_description = "FA3 fixture driver"
local function dir() return os.getenv("FA3_OUT") end
local function dump(subs, name)
    local f = assert(io.open(dir() .. "/" .. name, "w"))
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            local keys = {}
            for k in pairs(l.extra) do keys[#keys + 1] = k end
            table.sort(keys)
            f:write(l.text)
            for _, k in ipairs(keys) do f:write("\t", k, "=", l.extra[k]) end
            f:write("\n")
        end
    end
    f:close()
end

local function find(subs, text)
    for i = 1, #subs do
        if subs[i].class == "dialogue" and subs[i].text:find(text, 1, true) then return i end
    end
end

aegisub.register_macro("fa3 dump", "", function(subs)
    dump(subs, os.getenv("FA3_NAME") .. ".lua-extra.txt")
    -- Aegisub saves only a modified file.
    local i = find(subs, "")
    subs[i] = subs[i]
end)

aegisub.register_macro("fa3 edit", "", function(subs)
    local i = find(subs, "Inside both")
    local l = subs[i]; l.extra["hikari.added"] = "x,y:z|w#"; subs[i] = l
    i = find(subs, "Shares the script value")
    l = subs[i]; l.text = l.text .. " (edited)"; subs[i] = l
    i = find(subs, "Second adjacent fold ends")
    l = subs[i]; l.extra["hikari.big"] = "1,2,3,4,5:6|7#8,9"; subs[i] = l
    i = find(subs, "First adjacent fold ends")
    l = subs[i]; l.extra = {}; subs[i] = l
    l = subs[find(subs, "Outer fold opens")]; l.text = "Copied fold line"; subs.append(l)
end)

aegisub.register_macro("fa3 touch", "", function(subs)
    local i = find(subs, "first")
    local l = subs[i]; l.text = l.text .. " (edited)"; subs[i] = l
end)
