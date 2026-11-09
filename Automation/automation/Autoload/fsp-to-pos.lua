-- Thai support by Jibrill: based on "Fsp for thai char" 1.2 by Jibrill, used and changed with permission.

local tr = aegisub.gettext

script_name = tr"Letter spacing to positioned clusters"
script_description = tr"Replace \\fsp letter spacing with one positioned line per character cluster, keeping marks on their base characters"
script_author = "Jibrill"
script_version = "2.0"

include("karaskel.lua")

local HARD_BREAK, SOFT_BREAK, HARD_SPACE = "\1", "\2", "\3"

local function copy_table(t)
    local u = {}
    for k, v in pairs(t) do u[k] = v end
    return u
end

local function split_clusters(text, soft_breaks_wrap)
    local marked = text:gsub("\\N", HARD_BREAK):gsub("\\n", SOFT_BREAK):gsub("\\h", HARD_SPACE)
    local clusters = {}
    for cluster in unicode.graphemes(marked) do
        if cluster == HARD_BREAK or (cluster == SOFT_BREAK and soft_breaks_wrap) then
            table.insert(clusters, {line_break = true})
        elseif cluster == SOFT_BREAK then
            table.insert(clusters, {text = " "})
        elseif cluster == HARD_SPACE then
            table.insert(clusters, {text = "\\h", measured = " "})
        else
            table.insert(clusters, {text = cluster})
        end
    end
    return clusters
end

local function parse_blocks(text)
    local blocks = {}
    local i = 1
    while i <= #text do
        local open = text:find("{", i, true)
        if open ~= i then
            table.insert(blocks, {text = text:sub(i, (open or #text + 1) - 1)})
            if not open then break end
        end
        local close = text:find("}", open, true)
        if not close then
            table.insert(blocks, {text = text:sub(open)})
            break
        end
        table.insert(blocks, {tags = text:sub(open + 1, close - 1)})
        i = close + 1
    end
    return blocks
end

local function apply_tags(style, line_style, styles, tags, spacing)
    for tag in tags:gsub("\\t%b()", ""):gmatch("\\([^\\]*)") do
        tag = tag:gsub("%s+$", "")
        local reset = tag:match("^r(.*)")
        if reset then
            local base = styles[reset] or line_style
            for k, v in pairs(base) do style[k] = v end
            if spacing.from_line then spacing.value = base.spacing end
        elseif tag:match("^fn") then
            style.fontname = tag ~= "fn" and tag:sub(3) or line_style.fontname
        elseif tag:match("^fscx[%d%.]+$") then
            style.scale_x = tonumber(tag:sub(5))
        elseif tag:match("^fscy[%d%.]+$") then
            style.scale_y = tonumber(tag:sub(5))
        elseif tag:match("^fsp[%d%.%-]*$") then
            if spacing.from_line then spacing.value = tonumber(tag:sub(4)) or line_style.spacing end
        elseif tag:match("^fs[%d%.]+$") then
            style.fontsize = tonumber(tag:sub(3))
        elseif tag == "b" then
            style.bold = line_style.bold
        elseif tag:match("^b%d+$") then
            style.bold = tonumber(tag:sub(2)) > 0
        elseif tag == "i" then
            style.italic = line_style.italic
        elseif tag:match("^i%d+$") then
            style.italic = tag == "i1"
        end
    end
end

local function kept_tags(tags)
    return tags:gsub("\\pos%b()", ""):gsub("\\fsp[%d%.%-]*", "")
end

local function anchor(meta, line)
    local an = tonumber(line.text:match("{[^}]-\\an([1-9])")) or line.styleref.align
    local halign = ({"left", "center", "right"})[(an - 1) % 3 + 1]
    local valign = an <= 3 and "bottom" or an <= 6 and "middle" or "top"
    local x, y = line.text:match("{[^}]-\\pos%(%s*([%d%.%-]+)%s*,%s*([%d%.%-]+)%s*%)")
    if x then
        return tonumber(x), tonumber(y), halign, valign
    end
    local left, right = line.eff_margin_l, meta.res_x - line.eff_margin_r
    local top, bottom = line.eff_margin_t, meta.res_y - line.eff_margin_b
    x = halign == "left" and left or halign == "right" and right or (left + right) / 2
    y = valign == "top" and top or valign == "bottom" and bottom or (top + bottom) / 2
    return x, y, halign, valign
end

local function measure(items)
    for _, item in ipairs(items) do
        if not item.line_break then
            item.width, item.height = aegisub.text_extents(item.style, item.measured or item.text)
            item.advance = item.width + item.spacing
        end
    end
end

local function wrap(items, max_width, should_wrap)
    local rows, row, width, last_space = {}, {}, 0, nil
    for _, item in ipairs(items) do
        if item.line_break then
            table.insert(rows, row)
            row, width, last_space = {}, 0, nil
        else
            if should_wrap and width > 0 and width + item.advance > max_width then
                local next_row = {}
                if last_space then
                    for i = last_space + 1, #row do table.insert(next_row, row[i]) end
                    for i = #row, last_space, -1 do table.remove(row, i) end
                end
                table.insert(rows, row)
                row, width, last_space = next_row, 0, nil
                for _, kept in ipairs(row) do width = width + kept.advance end
            end
            table.insert(row, item)
            width = width + item.advance
            if item.text:match("^%s+$") then last_space = #row end
        end
    end
    table.insert(rows, row)
    return rows
end

local function positioned_lines(meta, line, items)
    local soft_wrap = (tonumber(meta.wrapstyle) or 0) ~= 2
    local rows = wrap(items, meta.res_x - line.eff_margin_l - line.eff_margin_r, soft_wrap)
    local x, y, halign, valign = anchor(meta, line)

    local block_height = 0
    for _, row in ipairs(rows) do
        row.width, row.height = 0, 0
        for _, item in ipairs(row) do
            row.width = row.width + item.advance
            row.height = math.max(row.height, item.height)
        end
        if row.height == 0 then
            local _, height = aegisub.text_extents(line.styleref, " ")
            row.height = height
        end
        block_height = block_height + row.height
    end

    local top = valign == "bottom" and y - block_height or valign == "middle" and y - block_height / 2 or y
    local reset_spacing = line.styleref.spacing ~= 0 and "\\fsp0" or ""
    local out = {}
    for _, row in ipairs(rows) do
        local row_y = valign == "bottom" and top + row.height or valign == "middle" and top + row.height / 2 or top
        local cursor = halign == "center" and x - row.width / 2 or halign == "right" and x - row.width or x
        for _, item in ipairs(row) do
            if not item.text:match("^%s*$") and item.text ~= "\\h" then
                local item_x = halign == "center" and cursor + item.width / 2 or halign == "right" and cursor + item.width or cursor
                local new_line = copy_table(line)
                new_line.text = string.format("{\\pos(%.3f,%.3f)%s%s}%s", item_x, row_y, item.tags, reset_spacing, item.text)
                table.insert(out, new_line)
            end
            cursor = cursor + item.advance
        end
        top = top + row.height
    end
    return out
end

local function clusters_of(meta, styles, line, line_spacing)
    local style = copy_table(line.styleref)
    local spacing = {from_line = line_spacing == nil, value = line_spacing or line.styleref.spacing}
    local tags = ""
    local items = {}
    for _, block in ipairs(parse_blocks(line.text)) do
        if block.tags then
            tags = tags .. kept_tags(block.tags)
            apply_tags(style, line.styleref, styles, block.tags, spacing)
        else
            local measured_style = copy_table(style)
            measured_style.spacing = 0
            for _, item in ipairs(split_clusters(block.text, (tonumber(meta.wrapstyle) or 0) == 2)) do
                item.tags, item.style, item.spacing = tags, measured_style, spacing.value
                table.insert(items, item)
            end
        end
    end
    return items
end

local function fsp_to_pos(subs, sel)
    local dialog = {
        {class = "label", x = 0, y = 0, label = tr"Letter spacing"},
        {class = "floatedit", x = 1, y = 0, width = 2, name = "spacing", value = 0,
         hint = tr"Pixels added after each character cluster"},
    }
    local from_line, from_value, cancel = tr"Use the &line’s spacing", tr"Use &this value", tr"Cancel"
    local pressed, result = aegisub.dialog.display(dialog, {from_line, from_value, cancel}, {cancel = cancel})
    if not pressed or pressed == cancel then
        aegisub.cancel()
    end
    local spacing = pressed == from_value and result.spacing or nil

    local meta, styles = karaskel.collect_head(subs, false)
    local skipped = false
    for n = #sel, 1, -1 do
        aegisub.progress.task(string.format(tr"Spacing line %d of %d", #sel - n + 1, #sel))
        aegisub.progress.set((#sel - n) * 100 / #sel)
        local index = sel[n]
        local line = subs[index]
        if line.class == "dialogue" and not line.comment then
            if line.text:match("{[^}]-\\move%(") or line.text:match("{[^}]-\\p[1-9]") then
                skipped = true
            else
                karaskel.preproc_line_text(meta, styles, line)
                karaskel.preproc_line_pos(meta, styles, line)
                local items = clusters_of(meta, styles, line, spacing)
                if #items > 0 then
                    measure(items)
                    local generated = positioned_lines(meta, line, items)
                    line.comment = true
                    subs[index] = line
                    for i = #generated, 1, -1 do
                        subs.insert(index + 1, generated[i])
                    end
                end
            end
        end
    end
    if skipped then
        aegisub.log("%s\n", tr"Lines with \\move or a drawing were left as they are.")
    end
    aegisub.set_undo_point(script_name)
end

aegisub.register_macro(tr"Letter spacing to positioned clusters…", script_description, fsp_to_pos)
