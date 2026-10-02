-- N8 host fixture: macros exercising the isolated helper. Authored for the
-- tests (the unchanged bundled script is loaded separately).
script_name = "Host fixture"
script_description = 42 -- a number: read back as the string "42"
script_author = "tests"

runs = 0 -- script state kept between invocations

local function report(button, results)
    local keys = {}
    for k in pairs(results) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = { "button=" .. type(button) .. ":" .. tostring(button) }
    for _, k in ipairs(keys) do
        parts[#parts + 1] = k .. "=" .. type(results[k]) .. ":" .. tostring(results[k])
    end
    aegisub.debug.out(table.concat(parts, " "))
end

aegisub.register_macro("Dialog", "description read from the stack top", function()
    local button, results = aegisub.dialog.display({
        { class = "label", name = "lbl", label = "Hello", x = 0, y = 0 },
        { class = "Edit", name = "text", value = "v", text = "t", x = "1", y = 0, width = 2 },
        { class = "intedit", name = 7, value = "12", min = 5, max = 5 },
        { class = "floatedit", name = "f", value = 1.5, step = 0.25 },
        { class = "checkbox", name = "flag", label = "Flag", value = true },
        { class = "dropdown", name = "pick", items = { "a", 3, {}, "b" }, value = "b" },
    }, { "Go", "Stop" }, { ok = "Go", cancel = "Stop" })
    report(button, results)
end)

aegisub.register_macro("Default buttons", "", function()
    report(aegisub.dialog.display({ { class = "edit", name = "e", value = "x" } }))
end)

aegisub.register_macro("Count", "", function()
    runs = runs + 1
    aegisub.debug.out("runs=%d", runs)
    aegisub.debug.out(5, "above the trace level")
    aegisub.debug.out(1, "level %s", "one")
end)

aegisub.register_macro("Cancel self", "", function()
    aegisub.cancel()
end)

aegisub.register_macro("Wait for cancel", "", function()
    while not aegisub.progress.is_cancelled() do end
    aegisub.debug.out("saw cancel")
end)

aegisub.register_macro("Crash", "", function()
    os.exit(9)
end)

aegisub.register_macro("Noisy", "", function()
    print("print on stdout")
    io.write("io.write on stdout\n")
    io.stdout:flush()
    io.stderr:write("on stderr\n")
    aegisub.progress.set(10)
    aegisub.progress.set(5)
    aegisub.progress.set(50)
    aegisub.debug.out("still framed")
end)

aegisub.register_macro("Error", "", function()
    error("script failure")
end)

aegisub.register_macro("Bad dialog", "", function()
    aegisub.dialog.display({ { class = "slider" } })
end)

-- L2: the remaining control classes and their legacy coercions.
aegisub.register_macro("Controls", "", function()
    report(aegisub.dialog.display({
        { class = "color", name = "ass", value = "&H0000FF&" },
        { class = "color", name = "html", value = "#00ff00" },
        { class = "coloralpha", name = "alpha", value = "&H80FF0000" },
        { class = "color", name = "decimal", value = 255 },
        { class = "color", name = "lower", value = "&h0000ff&" },
        { class = "color", name = "junk", value = "zz" },
        { class = "intedit", name = "clamped", value = 50, min = 0, max = 10 },
        { class = "floatedit", name = "f", value = -3, min = 0, max = 10, step = 0.5 },
        { class = "textbox", name = "tb", value = "multi" },
        { class = "alpha", name = "al", value = "&H40&" },
    }, { "OK" }))
end)

-- L5: a native call that cannot see a cooperative cancel.
aegisub.register_macro("Stuck in native code", "", function()
    local ffi = require("ffi")
    if jit.os == "Windows" then
        ffi.cdef("void Sleep(unsigned long ms);")
        ffi.C.Sleep(30000)
    else
        ffi.cdef("unsigned int sleep(unsigned int seconds);")
        ffi.C.sleep(30)
    end
end)
