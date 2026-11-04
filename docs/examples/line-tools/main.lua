local PANEL = "timing-check"
local BAR_AREA = { x = 8, y = 10, w = 244, h = 92 }

local view = "chart"
local onlyOver = false
local results = {}
local tip = nil

local function length(text)
    local plain = text:gsub("{[^}]*}", ""):gsub("\\[Nnh]", " ")
    return select(2, plain:gsub("[^\128-\191]", ""))
end

local function check()
    local doc = hikari.document.read()
    results = {}
    if not doc then
        return
    end
    local skip = hikari.settings.get("skip-comments")
    for row, line in ipairs(doc.lines) do
        if not (skip and line.comment) then
            local seconds = math.max(0.001, (line["end"] - line.start) / 1000)
            results[#results + 1] = { id = line.id, row = row, cps = length(line.text) / seconds }
        end
    end
end

local function over(limit)
    local found = {}
    for _, r in ipairs(results) do
        if r.cps > limit then
            found[#found + 1] = r
        end
    end
    return found
end

local function chart(limit)
    local shown = onlyOver and over(limit) or results
    local count = math.min(#shown, 60)
    local top = limit * 1.5
    for i = 1, count do
        top = math.max(top, shown[i].cps)
    end
    local a = BAR_AREA
    local step = count > 0 and a.w / count or a.w
    local draw = {
        { op = "rect", x = a.x, y = a.y, w = a.w, h = a.h, stroke = "line" },
    }
    for i = 1, count do
        local r = shown[i]
        local h = math.max(1, a.h * r.cps / top)
        draw[#draw + 1] = { op = "rect", x = a.x + (i - 1) * step + 1, y = a.y + a.h - h, w = math.max(1, step - 2), h = h,
                            fill = r.cps > limit and "warning" or "accent", radius = 1 }
    end
    local limitY = a.y + a.h - a.h * limit / top
    draw[#draw + 1] = { op = "line", x1 = a.x, y1 = limitY, x2 = a.x + a.w, y2 = limitY, stroke = "danger", width = 1.5,
                        dash = { 4, 3 } }
    draw[#draw + 1] = { op = "text", x = a.x + a.w, y = limitY - 4, text = limit .. " CPS", fill = "danger",
                        align = "right", size = "small" }
    return {
        kind = "canvas", id = "chart", width = 260, height = 112, hover = true, draw = draw, tip = tip,
        name = "Reading speed of each line",
        description = #over(limit) .. " of " .. #results .. " lines are over " .. limit .. " characters per second.",
    }
end

local function list(limit)
    local rows = {}
    for _, r in ipairs(onlyOver and over(limit) or results) do
        rows[#rows + 1] = { id = tostring(r.id), text = "Line " .. r.row, detail = string.format("%.1f CPS", r.cps),
                            muted = r.cps <= limit }
    end
    if #rows == 0 then
        return { kind = "empty", text = "No lines to show." }
    end
    return { kind = "list", id = "lines", rows = rows }
end

local function draw()
    local limit = hikari.settings.get("cps")
    local found = #over(limit)
    hikari.panels.render(PANEL, {
        kind = "column",
        menu = {
            { id = "recheck", text = "Check again" },
            { id = "only-over", text = "Only lines over the limit", checked = onlyOver },
        },
        children = {
            { kind = "row", children = {
                { kind = "number", id = "cps", label = "Limit", value = limit, minimum = 5, maximum = 40, unit = "CPS" },
                { kind = "button", id = "check", text = "Check", style = "primary" },
            } },
            { kind = "choice", id = "view", value = view,
              options = { { value = "chart", text = "Chart" }, { value = "list", text = "List" } } },
            view == "chart" and chart(limit) or list(limit),
            { kind = "label", style = found > 0 and "body" or "muted", wrap = true,
              text = found == 0 and "Every line reads in time."
                  or found .. (found == 1 and " line is" or " lines are") .. " over " .. limit .. " CPS." },
        },
    })
end

local function barAt(x)
    local limit = hikari.settings.get("cps")
    local shown = onlyOver and over(limit) or results
    local count = math.min(#shown, 60)
    if count == 0 or x < BAR_AREA.x or x >= BAR_AREA.x + BAR_AREA.w then
        return nil
    end
    return shown[math.floor((x - BAR_AREA.x) / (BAR_AREA.w / count)) + 1]
end

local function choose(id)
    hikari.selection.set({ id }, id)
end

hikari.panels.on(PANEL, function(e)
    if e.kind == "show" or (e.kind == "click" and e.widget == "check") or (e.kind == "menu" and e.widget == "recheck") then
        check()
    elseif e.kind == "menu" and e.widget == "only-over" then
        onlyOver = not onlyOver
    elseif e.kind == "change" and e.widget == "cps" then
        hikari.settings.set("cps", math.floor(e.value))
    elseif e.kind == "change" and e.widget == "view" then
        view = e.value
    elseif e.kind == "select" or e.kind == "activate" then
        choose(tonumber(e.value))
        return
    elseif e.kind == "pointer" then
        local bar = e.type ~= "leave" and barAt(e.x) or nil
        if e.type == "press" and bar then
            choose(bar.id)
        end
        tip = bar and { x = e.x + 8, y = 4, text = string.format("Line %d: %.1f CPS", bar.row, bar.cps) } or nil
    end
    draw()
end)

hikari.events.on("document.changed", function()
    check()
    draw()
end)

hikari.commands.register("rename", function()
    local active = hikari.selection.get().active
    if not active then
        hikari.notify("Choose a line first.", "warning")
        return
    end
    local button, values = hikari.dialogs.ask("rename", {
        title = "Rename actor",
        body = { kind = "text", id = "actor", label = "Actor", value = "" },
        buttons = {
            { id = "rename", text = "Rename", style = "primary", default = true },
            { id = "cancel", text = "Cancel", cancel = true },
        },
    })
    if button ~= "rename" then
        return
    end
    hikari.document.edit("Rename actor", function(_, change)
        change:set_line(active, { actor = values.actor })
    end)
end)
