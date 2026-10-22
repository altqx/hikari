-- AD2 (#311): a macro whose dialog the selection shapes, for Repeat last
-- run and "Fill in my last answers". The script works out "count" from the
-- selection (the value the safe-prefill rule leaves it), sends a fixed
-- "tags", and adds a "clip" check box from three selected Lines on (a
-- changed dialog). A second dialog follows the first. Cancel at either is
-- reported, not raised, so the run still ends well.
script_name = "Repeat dialogs"
script_description = "AD2 fixtures"

local function report(tag, button, r)
    aegisub.debug.out(string.format("%s button=%s tags=%s count=%s flag=%s clip=%s\n", tag, tostring(button),
        tostring(r.tags), tostring(r.count), tostring(r.flag), tostring(r.clip)))
end

aegisub.register_macro("Two dialogs", "", function(subs, sel)
    local first = {
        { class = "label", label = "Tags", x = 0, y = 0 },
        { class = "edit", name = "tags", value = "\\blur", x = 1, y = 0 },
        { class = "intedit", name = "count", value = #sel, min = 0, max = 100, x = 1, y = 1 },
        { class = "checkbox", name = "flag", label = "Flag", value = true, x = 1, y = 2 },
    }
    if #sel >= 3 then
        first[#first + 1] = { class = "checkbox", name = "clip", label = "Clip", value = false, x = 1, y = 3 }
    end
    local button, r = aegisub.dialog.display(first, { "Apply", "Cancel" }, { ok = "Apply", cancel = "Cancel" })
    report("first", button, r)
    if not button then
        return
    end
    local again, s = aegisub.dialog.display({
        { class = "label", label = "Sure?", x = 0, y = 0 },
        { class = "checkbox", name = "flag", label = "Really", value = false, x = 0, y = 1 },
    }, { "Yes", "No" }, { ok = "Yes", cancel = "No" })
    report("second", again, s)
end)

-- A button label with an escaped "&&" and a mnemonic, for Repeat's tooltip.
aegisub.register_macro("Save and close", "", function()
    local button, r = aegisub.dialog.display({
        { class = "label", label = "Done?", x = 0, y = 0 },
        { class = "checkbox", name = "backup", label = "Keep a backup", value = true, x = 0, y = 1 },
    }, { "Save && &Close", "Cancel" }, { ok = "Save && &Close", cancel = "Cancel" })
    aegisub.debug.out(string.format("button=%s backup=%s\n", tostring(button), tostring(r.backup)))
end)

-- A question (labels and buttons only), as DependencyControl's feed trust
-- prompt, then a form: Repeat asks the question again and replays the form.
aegisub.register_macro("Trust prompt", "", function()
    local trust = aegisub.dialog.display({
        { class = "label", label = "Trust https://example.org/feed.json and proceed?", x = 0, y = 0 },
    }, { "Cancel", "Trust this time" }, { cancel = "Cancel" })
    aegisub.debug.out(string.format("trust=%s\n", tostring(trust)))
    if not trust then
        return
    end
    local button, r = aegisub.dialog.display({
        { class = "edit", name = "tags", value = "\\blur", x = 0, y = 0 },
    }, { "Apply", "Cancel" }, { ok = "Apply", cancel = "Cancel" })
    aegisub.debug.out(string.format("form=%s tags=%s\n", tostring(button), tostring(r.tags)))
end)
