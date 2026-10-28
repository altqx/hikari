-- AS1 fixture: macro names with "/" open submenus of the Automation menu.
-- Each macro appends its name to the selected Lines.
script_name = "Nested macros"

local function append(name)
    return function(subs, sel)
        for _, i in ipairs(sel) do
            local line = subs[i]
            line.text = line.text .. " " .. name
            subs[i] = line
        end
    end
end

for _, name in ipairs({
    "Flat",
    "Group/Sub/Deep",
    "Group/Shallow",
    "Other/Item",
    "Group/Sub/Deeper",
    "Group",
    "Solo",
    "Solo/Child",
    "Gap//Inner",
    "Trailing/",
    "/Leading",
}) do
    aegisub.register_macro(name, "", append(name))
end
