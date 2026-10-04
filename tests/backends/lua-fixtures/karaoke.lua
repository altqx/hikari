-- L6 fixture: aegisub.parse_karaoke_data and aegisub.get_frequency_peaks,
-- which legacy adds to the aegisub table when a macro runs (AutoToFile).
script_name = "Karaoke"

local atLoad = type(aegisub.parse_karaoke_data) .. "," .. type(aegisub.get_frequency_peaks)

local function show(...)
    local t = {}
    for i = 1, select('#', ...) do t[#t + 1] = tostring((select(i, ...))) end
    aegisub.log("%s", table.concat(t, ","))
end

local function dialogue(text, translation)
    return { class = "dialogue", comment = false, layer = 0, start_time = 1000, end_time = 3500, style = "Default",
             actor = "", margin_l = 0, margin_r = 0, margin_t = 0, effect = "", text = text,
             text_translation = translation }
end

local function syllables(line)
    local out = {}
    local i = 0
    while line[i] do
        local s = line[i]
        out[#out + 1] = string.format("%d:%d,%d,%d,%s,%s|%s", i, s.duration, s.start_time, s.end_time, s.tag, s.text,
                                      s.text_stripped)
        i = i + 1
    end
    return table.concat(out, " ")
end

aegisub.register_macro("Presence", "", function()
    show(atLoad, type(aegisub.parse_karaoke_data), type(aegisub.get_frequency_peaks))
end)

aegisub.register_macro("Karaoke", "", function()
    for _, line in ipairs({ dialogue("{\\k10}a{\\k20}b{\\kf30}c"), dialogue("{\\b1\\k10}ab{\\i1}c{\\k5}d"),
                            dialogue("Hello {\\b1}world"), dialogue("", "{\\k5}x") }) do
        aegisub.log("%s", syllables(aegisub.parse_karaoke_data(line)))
    end
    local line = dialogue("{\\k10}a")
    show(rawequal(aegisub.parse_karaoke_data(line), line), line.class, line[1].text)
    for _, bad in ipairs({ { class = "info", key = "k", value = "v" }, { class = "unknown" }, "text" }) do
        show(select(2, pcall(aegisub.parse_karaoke_data, bad)))
    end
end)

local function report(ok, ...)
    if not ok then return show((...)) end
    local times, intensities = ...
    show("n=" .. select('#', ...), "t=" .. table.concat(times or {}, ";"), "i=" .. table.concat(intensities or {}, ";"))
end

local function peaks(...)
    report(pcall(aegisub.get_frequency_peaks, ...))
end

aegisub.register_macro("Peaks", "", function()
    peaks(0, 1000, 0, 100, 0)
    peaks(0, 1000, 0, 100, 5000)
    peaks(0, 1000, 0, 100, -7)
    peaks(1.9, "2000", 3.5, 4, 0)
    peaks(500, 500, 0, 0, 0)
    peaks(-1, 10, 0, 0, 0)
    peaks(0, 10, 0, 0)
    peaks(0, 10, "x", 0, 0)
end)
