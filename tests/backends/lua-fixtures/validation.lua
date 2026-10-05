-- S4 fixture: validation functions (legacy LuaCommand::Validate before Run,
-- Automation.cpp:936-969 at 20d647c4). The script's macros are run with
-- RunValidated; the tests read what each saw.
script_name = "Validation"

-- Valid only with exactly one selected line; the macro marks it.
aegisub.register_macro("One line", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "ran:" .. l.text
    subs[sel[1]] = l
end, function(subs, sel, active)
    aegisub.log("validated %d %d %d", #subs, #sel, active)
    return #sel == 1, "help text"
end)

aegisub.register_macro("Never", "", function(subs)
    local l = subs[4]
    l.text = "should not run"
    subs[4] = l
end, function()
    return false
end)

aegisub.register_macro("Broken", "", function() end, function()
    error("validation broke")
end)

-- Validation inserts an info line; the macro sees it and its rows moved by
-- one (legacy Run counts SInfoSize + StylesSize again).
aegisub.register_macro("Edits while validating", "", function(subs, sel, active)
    local l = subs[sel[1]]
    l.text = string.format("%s|%d|%d|%s", l.text, sel[1], active, subs[3].key)
    subs[sel[1]] = l
end, function(subs)
    subs.insert(3, { class = "info", section = "[Script Info]", key = "Validated", value = "yes" })
    return true
end)

-- Validation edits the subtitles and then answers false (or raises an
-- error): the edits are dropped (S4-validation-edits).
aegisub.register_macro("Edits then false", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "should not run"
    subs[sel[1]] = l
end, function(subs, sel)
    local l = subs[sel[1]]
    l.text = "edited while validating"
    subs[sel[1]] = l
    subs.insert(3, { class = "info", section = "[Script Info]", key = "Validated", value = "yes" })
    return false
end)

aegisub.register_macro("Edits then error", "", function() end, function(subs, sel)
    subs.delete(sel[1])
    error("validation broke after editing")
end)

aegisub.register_macro("Unvalidated", "", function(subs, sel)
    local l = subs[sel[1]]
    l.text = "plain"
    subs[sel[1]] = l
end)
