-- AD1 (#310): dialogs from real scripts, each call as the script makes it
-- (controls, buttons and button ids), with the script's own reading of the
-- answer reduced to a report. Pinned sources:
-- * arch1t3cht GitSigns, Configure: TypesettingTools/arch1t3cht-Aegisub-Scripts
--   5c3c782196998fc231a5e3195d69797f5d745800 macros/arch.GitSigns.moon#L218-L241
-- * lyger SemiColorCalc: lyger/Aegisub_automation_scripts
--   6c3ea551afbe0c404dfd1faba44b55b9db87018a macros/lyger.SemiColorCalc.lua#L29-L52
-- * DependencyControl Toolbox: TypesettingTools/DependencyControl
--   c4101aeed1a92d26b4ada28720dd7e3d5845d3b4 macros/l0.DependencyControl.Toolbox.moon
--   (getScriptListDlg #L177-L187, confirmDialog #L356-L360, feed trust #L206-L208,
--   buildConfigForm and globalConfig #L747-L800)
script_name = "Script dialogs"
script_description = "AD1 fixtures"

local function report(tag, button, results)
    local keys = {}
    for k in pairs(results or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = { tag, "button=" .. type(button) .. ":" .. tostring(button) }
    for _, k in ipairs(keys) do
        parts[#parts + 1] = k .. "=" .. type(results[k]) .. ":" .. tostring(results[k])
    end
    aegisub.debug.out(table.concat(parts, " "))
end

aegisub.register_macro("GitSigns Configure", "", function()
    local ok, cancel = "Save", "Cancel"
    local btn, result = aegisub.dialog.display({ {
        class = "label", label = "Git path: ", x = 0, y = 0, width = 1, height = 1,
    }, {
        class = "edit", name = "git_path", hint = "Path to git executable", value = "git",
        x = 1, y = 0, width = 1, height = 1,
    } }, { ok, cancel }, { ok = ok, cancel = cancel })
    -- "return if not btn", then the config is written.
    report(btn and "saved" or "not saved", btn, result)
end)

local conf = {
    { x = 0, y = 0, width = 4, height = 1, class = "label", label = "Color(s):" },
    c1 = { x = 0, y = 1, width = 1, height = 1, class = "checkbox", name = "c1", label = "1", value = false },
    c2 = { x = 1, y = 1, width = 1, height = 1, class = "checkbox", name = "c2", label = "2", value = false },
    c3 = { x = 2, y = 1, width = 1, height = 1, class = "checkbox", name = "c3", label = "3", value = false },
    c4 = { x = 3, y = 1, width = 1, height = 1, class = "checkbox", name = "c4", label = "4", value = false },
    { x = 0, y = 2, width = 2, height = 1, class = "label", label = "Background:" },
    bg = { x = 2, y = 2, width = 2, height = 1, class = "color", name = "bg" },
    { x = 0, y = 3, width = 2, height = 1, class = "label", label = "Target:" },
    tg = { x = 2, y = 3, width = 2, height = 1, class = "color", name = "tg" },
    { x = 0, y = 4, width = 2, height = 1, class = "label", label = "Opacity (%):" },
    al = { x = 2, y = 4, width = 2, height = 1, class = "floatedit", max = 100, min = 0, name = "al", value = 50, step = 1 },
}

aegisub.register_macro("SemiColorCalc", "", function()
    local pressed, results = aegisub.dialog.display(conf, { "Go", "Cancel" }, { ok = "Go", cancel = "Cancel" })
    -- "if pressed=="Cancel" then aegisub.cancel() end": in Aegisub never true.
    report(pressed == "Cancel" and "cancelled" or "calculated", pressed, results)
end)

local function script_list(macros, modules)
    local m, d = { "" }, { "" }
    for _, v in ipairs(macros) do m[#m + 1] = v end
    for _, v in ipairs(modules) do d[#d + 1] = v end
    return {
        { label = "Automation Scripts: ", class = "label", x = 0, y = 0, width = 1, height = 1 },
        { name = "macro", class = "dropdown", x = 1, y = 0, width = 1, height = 1, items = m, value = "" },
        { label = "Modules: ", class = "label", x = 0, y = 1, width = 1, height = 1 },
        { name = "module", class = "dropdown", x = 1, y = 1, width = 1, height = 1, items = d, value = "" },
    }
end

aegisub.register_macro("Toolbox install", "", function()
    local btn, res = aegisub.dialog.display(script_list({ "l0.ASSWipe", "petzku.EncodeClip" }, { "l0.Functional" }))
    -- "return unless btn"
    report(btn and "install" or "nothing", btn, res)
end)

aegisub.register_macro("Toolbox confirm", "", function()
    local btn = aegisub.dialog.display({ { class = "label", x = 0, y = 0, width = 1, height = 1,
        label = "Restore the default settings?" } }, { "Yes", "No" }, { ok = "Yes", cancel = "No" })
    report(btn == "Yes" and "confirmed" or "declined", btn, {})
end)

aegisub.register_macro("Toolbox feed trust", "", function()
    local btn = aegisub.dialog.display({
        { class = "label", x = 0, y = 0, width = 3, height = 1, label = "Trust ghuc://example/feed.json?" },
    }, { "Trust", "Fetch once", "Block", "Skip" }, { ok = "Fetch once", cancel = "Skip" })
    report("feed", btn, {})
end)

aegisub.register_macro("Toolbox config", "", function()
    local btn, res = aegisub.dialog.display({
        { class = "label", x = 0, y = 0, width = 2, height = 1, label = "— Updates —" },
        { class = "label", x = 0, y = 1, width = 1, height = 1, label = "Check for updates" },
        { class = "checkbox", x = 1, y = 1, width = 1, height = 1, name = "updates.updaterEnabled", label = "", value = true },
        { class = "label", x = 0, y = 2, width = 1, height = 1, label = "Interval (seconds)" },
        { class = "intedit", x = 1, y = 2, width = 1, height = 1, name = "updates.updateInterval", value = 302400, min = 0 },
        { class = "label", x = 2, y = 0, width = 2, height = 1, label = "— Logging —" },
        { class = "label", x = 2, y = 1, width = 1, height = 1, label = "Trace level" },
        { class = "dropdown", x = 3, y = 1, width = 1, height = 1, name = "logging.traceLevel",
          items = { "0", "1", "2", "3", "4", "5" }, value = "3" },
        { class = "label", x = 2, y = 2, width = 1, height = 1, label = "Log directory" },
        { class = "edit", x = 3, y = 2, width = 1, height = 1, name = "paths.logDir", text = "?user/log" },
    }, { "Save", "Restore defaults", "Cancel" }, { ok = "Save", cancel = "Cancel" })
    -- "break if not btn or btn == buttons.cancel"
    report(btn and "applied" or "closed", btn, res)
end)

-- wx's escape fallback made Esc press the only affirmative button.
aegisub.register_macro("Ok only", "", function()
    report("ok-only", aegisub.dialog.display({ { class = "label", label = "x" } }, { "Go" }, { ok = "Go" }))
end)

aegisub.register_macro("Invalid id", "", function()
    local ok, err = pcall(aegisub.dialog.display, { { class = "label", label = "x" } }, { "A" }, { ok = "Nope" })
    aegisub.debug.out("invalid " .. tostring(ok) .. " " .. tostring(err):gsub("^.-: ", ""))
end)

aegisub.register_macro("Unknown id", "", function()
    report("unknown", aegisub.dialog.display({ { class = "label", label = "x" } }, { "A", "B" }, { weird = "A", cancel = "B" }))
end)

aegisub.register_macro("Affirmative ids", "", function()
    report("affirmative", aegisub.dialog.display({ { class = "label", label = "x" } },
        { "&Back", "&Yes", "Save &as", "Close" }, { yes = "&Yes", save = "Save &as", close = "Close" }))
end)
