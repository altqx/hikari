-- Y4-playres-ask: subtitles.script_resolution (and karaskel's collect_head,
-- which calls it) asks the host while PlayResX or PlayResY is missing.
script_name = "Script resolution"

local function playres(subs)
    local keys = {}
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "info" and (l.key == "PlayResX" or l.key == "PlayResY") then
            keys[#keys + 1] = l.key .. "=" .. l.value
        end
    end
    return table.concat(keys, ";")
end

aegisub.register_macro("Read resolution", "", function(subs)
    local w, h = subs.script_resolution()
    local w2, h2 = subs.script_resolution()
    aegisub.log("%d x %d, %d x %d, %s", w, h, w2, h2, playres(subs))
end)

aegisub.register_macro("Karaskel head", "", function(subs)
    include("karaskel.lua")
    local meta = karaskel.collect_head(subs, false)
    aegisub.log("%d x %d", meta.res_x, meta.res_y)
end)

aegisub.register_macro("Edit and read", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited"
    subs[sel[1]] = l
    local w, h = subs.script_resolution()
    aegisub.log("%d x %d", w, h)
end)

aegisub.register_macro("Edit only", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited"
    subs[sel[1]] = l
end)

-- The validation function reads it: asked there, and the processing
-- function sees the same values without a second question.
aegisub.register_macro("Validate and read", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited"
    subs[sel[1]] = l
    local w, h = subs.script_resolution()
    aegisub.log("run %d x %d", w, h)
end, function(subs)
    local w, h = subs.script_resolution()
    return w > 0 and h > 0
end)

aegisub.register_macro("Validate refuses", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited"
    subs[sel[1]] = l
end, function(subs)
    subs.script_resolution()
    return false
end)

-- Reads it, then runs until the run is cancelled.
aegisub.register_macro("Read and wait", "", function(subs)
    subs.script_resolution()
    while not aegisub.progress.is_cancelled() do
        aegisub.progress.set(50)
    end
end)
