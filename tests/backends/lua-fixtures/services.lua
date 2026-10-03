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
    show(aegisub.frame_from_ms(1))
end)
