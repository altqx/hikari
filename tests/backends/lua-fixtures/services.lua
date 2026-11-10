-- L3 fixture: host services with the legacy argument handling and returns.
script_name = "Services"

local function show(...)
    local t = {}
    for i = 1, select('#', ...) do t[#t + 1] = tostring((select(i, ...))) end
    aegisub.log("%s", table.concat(t, ","))
end

aegisub.register_macro("Media", "", function(subs)
    show(aegisub.frame_from_ms(1000), aegisub.ms_from_frame(25))
    local w, h, ar, kind = aegisub.video_size()
    show(w, h, kind, math.abs(ar - 16 / 9) < 1e-6)
    local kf = aegisub.keyframes()
    show(#kf, kf[2])
    show(aegisub.get_audio_selection())
    local p = aegisub.project_properties()
    show(p.video_position, p.video_file, p.audio_file, p.keyframes_file, p.timecodes_file, p.video_zoom,
         p.automation_scripts, p.active_row)
    show(aegisub.file_name(), aegisub.decode_path("?user/x"))
    local f = aegisub.get_frame(3, true)
    show(f:width(), f:height(), f:getPixel(1, 0), f:getPixelFormatted(1, 0), f:getPixel(5, 5))
end)

aegisub.register_macro("Fork API", "", function()
    local v = aegisub.version
    show(v.string, v.major, v.minor, v.patch, v.build)
    local ffi = require 'ffi'
    local f = aegisub.get_frame(3, true)
    show(f:getPixel(1, 0))
    local data, pitch, flipped = f:data()
    local bytes = ffi.cast("uint8_t *", data)
    show(type(data), pitch, flipped, bytes[4], bytes[5], bytes[6], bytes[7])
end)

aegisub.register_macro("Frame times", "", function()
    local t = {}
    for ms = -100, 700, 7 do t[#t + 1] = tostring(aegisub.frame_from_ms(ms)) end
    for frame = -3, 20 do t[#t + 1] = tostring(aegisub.ms_from_frame(frame)) end
    aegisub.log("%s", table.concat(t, ","))
end)

aegisub.register_macro("Pixel loop", "", function()
    local f = aegisub.get_frame(3, true)
    local reads = 0
    for i = 1, 100 do
        if f:getPixel(1, 0) == 48 then reads = reads + 1 end
    end
    local r, g, b = f:getPixel(1, 0)
    local data, pitch = f:data()
    show(reads, r, g, b, f:getPixelFormatted(1, 0), type(data), pitch)
end)

aegisub.register_macro("Unavailable", "", function()
    show(aegisub.frame_from_ms(1), aegisub.video_size(), aegisub.keyframes(), aegisub.get_frame(1),
         aegisub.project_properties(), aegisub.file_name(), aegisub.get_audio_selection(),
         aegisub.dialog.open("t", "", "", "", false, true), aegisub.gui.get_cursor())
end)

aegisub.register_macro("Text", "", function(subs)
    show(aegisub.text_extents(subs[3], "Hello"))
    show(aegisub.text_extents(subs[3], ""))
    show(select('#', aegisub.text_extents(subs[4], "x")))
    local ok, err = pcall(aegisub.text_extents, "style", "x")
    show(err)
    ok, err = pcall(aegisub.text_extents, subs[3], {})
    show(err)
end)

aegisub.register_macro("Measure many", "", function(subs)
    show((aegisub.text_extents(subs[3], "Hello")), (aegisub.text_extents(subs[3], "Hello")),
         (aegisub.text_extents(subs[3], "World")))
    local many = aegisub.__karaskel_impl.measure_many
    local e = many(subs[3], {"a", "Hello", "b", "", "c", "a"})
    show(#e, e[5], e[9], e[6], e[13], e[17], e[21])
    show(#many(subs[4], {"x"}))
    local ok, err = pcall(many, "style", {"x"})
    show(err)
    ok, err = pcall(many, subs[3], {"x", nil, "y"}, 3)
    show(err)
end)

aegisub.register_macro("Clipboard", "", function()
    local ffi = require 'ffi'
    pcall(ffi.cdef, "void free(void *);")
    local impl = aegisub.__init_clipboard()
    show(impl.set("copied"))
    local p = impl.get()
    local s = ffi.string(p)
    ffi.C.free(p)
    show(s)
end)

aegisub.register_macro("Pickers", "", function()
    local files = aegisub.dialog.open("Open", "/d", "f.txt", "Text|*.txt", true, false)
    show(#files, files[1], files[2])
    show(aegisub.dialog.open("Open", "/d", "f.txt", "Text|*.txt"))
    show(aegisub.dialog.save("Save", "/d", "out.txt", "", true))
end)

aegisub.register_macro("Gui", "", function()
    show(aegisub.gui.get_cursor())
    aegisub.gui.set_cursor(5)
    show(aegisub.gui.get_selection())
    aegisub.gui.set_selection(2, 4)
    show(aegisub.gui.is_modified())
    show(select('#', aegisub.set_status_text("busy")))
end)

aegisub.register_macro("Wait", "", function()
    show(aegisub.keyframes()[1])
end)
