-- KE1 fixture: aegisub.hikari, the Line a generated line came from and the
-- run's report.
script_name = "HikariSub API"

local function listed(sources)
    local keys = {}
    for i in pairs(sources) do
        table.insert(keys, i)
    end
    table.sort(keys)
    local out = {}
    for _, i in ipairs(keys) do
        table.insert(out, i .. "=" .. sources[i])
    end
    return table.concat(out, ",")
end

aegisub.register_macro("Mark", "", function(subs)
    local l = subs[4]
    l.text = "one, generated"
    subs.append(l)
    subs.append(l)
    aegisub.hikari.set_source(7, 4)
    aegisub.hikari.set_source(8, 4)
    assert(not pcall(aegisub.hikari.set_source, 1, 4), "an info line is no dialogue line")
    assert(not pcall(aegisub.hikari.set_source, 8, 7), "a line the macro added is no Line of the Document")
    assert(not pcall(aegisub.hikari.set_source, 5, 4), "only lines the macro added are marked")
    assert(listed(aegisub.hikari.sources()) == "7=4,8=4")
    aegisub.hikari.report("first")
    aegisub.hikari.report("marked")
end)

aegisub.register_macro("Sources", "", function(subs)
    subs.insert(4, subs[6])
    subs[9] = subs[9] -- the slot keeps its Line and its mark
    aegisub.hikari.report(listed(aegisub.hikari.sources()))
end)
