-- O5 fixture: aegisub.gettext as legacy get_translation (Automation.cpp:83-88),
-- an unchanged script that knows nothing of the catalog format.
local tr = aegisub.gettext
script_name = tr"Search bar" -- at the top level, while the script loads
local loaded = script_name

local function show(...)
    local t = {}
    for i = 1, select('#', ...) do t[#t + 1] = tostring((select(i, ...))) end
    aegisub.log("%s", table.concat(t, "|"))
end

aegisub.register_macro(tr"Search bar", "", function()
    show(loaded, tr"Search bar", tr"no such key", tr"%d element", tr(12), tr"")
    -- the script formats a returned printf text itself
    show(string.format(tr"%d element", 3))
    show(select('#', tr"Search bar"), type(tr"Search bar"))
    show(pcall(tr))
    show(pcall(tr, {}))
    show(#tr("Search\0bar"), tr("Search\0bar"))
    show(#tr("\255bad"))
end)
