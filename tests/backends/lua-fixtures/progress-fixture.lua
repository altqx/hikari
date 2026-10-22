-- AD3 fixture: macros for the progress card and the run log.
script_name = "Progress fixture"

-- Ends at once, logging nothing.
aegisub.register_macro("Quick", "", function() end)

-- Works until cancelled, logging nothing.
aegisub.register_macro("Busy", "", function()
    aegisub.progress.task("Working")
    while not aegisub.progress.is_cancelled() do
        aegisub.progress.set(25)
    end
end)

-- Writes at every level, without one, and a line in two parts.
aegisub.register_macro("Levels", "", function()
    for level = 0, 5 do
        aegisub.debug.out(level, "level %d\n", level)
    end
    aegisub.log("no level\n")
    aegisub.log("first part, ")
    aegisub.log("second part\n")
end)

-- Writes a line, then waits for Cancel.
aegisub.register_macro("Talk then wait", "", function()
    aegisub.log(2, "started\n")
    while not aegisub.progress.is_cancelled() do end
end)

-- Writes a line, then raises an error.
aegisub.register_macro("Fail", "", function()
    aegisub.log(1, "about to fail\n")
    error("boom")
end)

-- Asks first, then ends at once.
aegisub.register_macro("Ask", "", function()
    aegisub.dialog.display({ { class = "label", label = "Go?", x = 0, y = 0 } })
end)

-- Not valid: it never runs.
aegisub.register_macro("Never", "", function() end, function() return false end)

-- The screenshots' run: a task, a progress and lines at several levels,
-- then waits for Cancel.
aegisub.register_macro("Retime karaoke", "", function()
    aegisub.progress.task("Retiming line 31 of 120")
    aegisub.progress.set(26)
    aegisub.debug.out(3, "Reading 120 lines\n")
    aegisub.debug.out(2, "Line 12: no \\k tags, skipped\n")
    aegisub.log("Syllables: 948\n")
    aegisub.debug.out(1, "Line 31: overlapping syllables\n")
    while not aegisub.progress.is_cancelled() do end
end)

-- Works 1.5 s, asks, then works until cancelled.
aegisub.register_macro("Work, ask, work", "", function()
    local t = os.clock()
    while os.clock() - t < 1.5 do end
    aegisub.dialog.display({ { class = "label", label = "Go on?", x = 0, y = 0 } })
    while not aegisub.progress.is_cancelled() do end
end)

-- Writes nothing, twice.
aegisub.register_macro("Empty log", "", function()
    aegisub.log("")
    aegisub.debug.out(2, "")
end)
