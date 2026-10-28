-- S1 fixture: macros run from the shell against the editing target.
script_name = "Shell fixture"

aegisub.register_macro("Prefix from dialog", "", function(subs, sel)
    local pressed, result = aegisub.dialog.display({
        { class = "label", label = "Prefix", x = 0, y = 0 },
        { class = "edit", name = "prefix", text = "P:", x = 1, y = 0 },
    })
    if not pressed then aegisub.cancel() end
    for _, i in ipairs(sel) do
        local line = subs[i]
        line.text = result.prefix .. line.text
        subs[i] = line
    end
    local frame = aegisub.get_frame(aegisub.frame_from_ms(1001))
    local w, h = aegisub.video_size()
    aegisub.log("%s", string.format("frame %d, %dx%d, video %dx%d",
        aegisub.frame_from_ms(1001), frame:width(), frame:height(), w, h))
end)

aegisub.register_macro("Wait for cancel", "", function()
    while not aegisub.progress.is_cancelled() do
        aegisub.progress.set(50)
    end
end)
