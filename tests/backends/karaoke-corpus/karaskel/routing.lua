-- Runs karaskel.preproc_line both ways on fresh copies of each dialogue line, the Lua stages and the
-- helper's native one, and reports where the tables, their pairs() order, the errors or the debug output
-- differ, and which runs the native one took.
script_name = "karaskel routing"

include("karaskel.lua")

local function dump(root)
	local seen, out = {}, {}
	local function name(k)
		return type(k) == "number" and string.format("[%.17g]", k) or "." .. tostring(k)
	end
	local function walk(v, path)
		local kind = type(v)
		if kind == "table" then
			if seen[v] then
				table.insert(out, path .. " = @" .. seen[v])
				return
			end
			seen[v] = path
			table.insert(out, path .. " = {}")
			for k, child in pairs(v) do
				walk(child, path .. name(k))
			end
		elseif kind == "number" then
			table.insert(out, path .. " = " .. string.format("%.17g", v))
		else
			table.insert(out, path .. " = " .. kind .. " " .. tostring(v))
		end
	end
	walk(root, "")
	return out
end

local function json_string(s)
	return '"' .. s:gsub('[%c"\\]', function(c)
		return string.format("\\u%04x", c:byte())
	end) .. '"'
end

local function first_difference(a, b)
	for i = 1, math.max(#a, #b) do
		if a[i] ~= b[i] then
			local function around(t)
				return table.concat({ tostring(t[i - 1]), tostring(t[i]), tostring(t[i + 1]), tostring(t[i + 2]) }, "; ")
			end
			return string.format("at %d: %s | %s", i, around(a), around(b))
		end
	end
end

local impl = aegisub.__karaskel_impl
local own_native = impl.preproc_line
local ran

local function counted(...)
	ran = own_native(...)
	return ran
end

-- The outcome of `call` on line: its dump, its error and what debug.out got meanwhile.
local function outcome(call, line)
	local said = {}
	local host_out = aegisub.debug.out
	local recording = aegisub.__recording_debug
	if recording then
		aegisub.debug.out = function(level, ...)
			table.insert(said, tostring(level) .. ":" .. string.format(...))
		end
	end
	local ok, err = pcall(call, line)
	aegisub.debug.out = host_out
	local out = dump({ line = line, error = (not ok) and tostring(err) or false })
	for _, s in ipairs(said) do
		table.insert(out, "said " .. s)
	end
	return out
end

local function compare(subs, meta, styles, fresh, label, report)
	local lua = outcome(function(l) karaskel.preproc_line_pos(meta, styles, l) end, fresh())
	impl.preproc_line = counted
	ran = false
	local routed = outcome(function(l) karaskel.preproc_line(subs, meta, styles, l) end, fresh())
	impl.preproc_line = own_native
	table.insert(report.runs, label .. (ran and ":native" or ":lua"))
	local diff = first_difference(lua, routed)
	if diff then
		table.insert(report.mismatches, label .. " " .. diff)
	end
end

local function encode(report)
	local runs, mismatches = {}, {}
	for _, r in ipairs(report.runs) do
		table.insert(runs, json_string(r))
	end
	for _, m in ipairs(report.mismatches) do
		table.insert(mismatches, json_string(m))
	end
	return '{"runs":[' .. table.concat(runs, ",") .. '],"mismatches":[' .. table.concat(mismatches, ",") .. "]}"
end

local function each_dialogue(subs, f)
	for i = 1, #subs do
		if subs[i].class == "dialogue" then
			f(i)
		end
	end
end

aegisub.register_macro("Every line", "", function(subs)
	local meta, styles = karaskel.collect_head(subs, true)
	local report = { runs = {}, mismatches = {} }
	each_dialogue(subs, function(i)
		compare(subs, meta, styles, function() return subs[i] end, "line " .. i, report)
	end)
	aegisub.hikari.report(encode(report))
end)

local function copy(t)
	local out = {}
	for k, v in pairs(t) do
		out[k] = v
	end
	return out
end

-- Each case runs on the file's first dialogue line, its Style changed as the case says.
local cases = {
	{ "plain", function(styles, line) end },
	{ "extra style field", function(styles, line) styles[line.style].hikari_extra = 1 end },
	{ "missing style field", function(styles, line) styles[line.style].margin_l = nil end },
	{ "style without class", function(styles, line) styles[line.style].class = nil end },
	{ "style fontsize text", function(styles, line) styles[line.style].fontsize = "big" end },
	{ "style margins apart", function(styles, line) styles[line.style].margin_b = styles[line.style].margin_t + 7 end },
	{ "style align text", function(styles, line) styles[line.style].align = "8" end },
	{ "style align outside", function(styles, line) styles[line.style].align = 11 end },
	{ "style edited", function(styles, line) styles[line.style].fontsize = styles[line.style].fontsize * 1.5 end },
	{ "style not found", function(styles, line) line.style = "Nowhere" end },
	{ "style not a table", function(styles, line) styles[line.style] = 5 end },
	{ "styles with a metatable", function(styles, line) setmetatable(styles, {}) end },
	{ "meta res text", function(styles, line, meta) meta.res_x = "1280" end },
	{ "meta factor edited", function(styles, line, meta) meta.video_x_correct_factor = 0.75 end },
	{ "line time fraction", function(styles, line) line.end_time = line.end_time + 0.5 end },
	{ "line margins apart", function(styles, line) line.margin_b = line.margin_t + 3 end },
	{ "line already sized", function(styles, line) line.styleref = styles[line.style] end },
	{ "line already parsed", function(styles, line, meta) karaskel.preproc_line_text(meta, styles, line) end },
	{ "line not dialogue", function(styles, line) line.class = "comment" end },
	{ "text_extents wrapped", function() aegisub.text_extents = function(...) return __host_extents(...) end end },
	{ "debug.out wrapped", function(styles, line) aegisub.__recording_debug = true; line.style = "Nowhere" end },
	{ "stage replaced", function()
		karaskel.preproc_line_size = function(...) __replaced = true; return __own_size(...) end
	end },
}

aegisub.register_macro("Cases", "", function(subs)
	local report = { runs = {}, mismatches = {} }
	for _, case in ipairs(cases) do
		local label, change = case[1], case[2]
		local meta, styles = karaskel.collect_head(subs, true)
		local first
		each_dialogue(subs, function(i) first = first or i end)
		local copied = { n = styles.n }
		for k, v in pairs(styles) do
			copied[k] = type(v) == "table" and copy(v) or v
		end
		local line = subs[first]
		__host_extents, __own_size = aegisub.text_extents, karaskel.preproc_line_size
		local host_extents, own_size = aegisub.text_extents, karaskel.preproc_line_size
		change(copied, line, meta)
		local base = line
		compare(subs, meta, copied, function() return copy(base) end, label, report)
		aegisub.text_extents, karaskel.preproc_line_size = host_extents, own_size
		aegisub.__recording_debug = nil
		if __replaced then
			table.insert(report.runs, label .. ":replaced stage ran")
			__replaced = nil
		end
	end
	aegisub.hikari.report(encode(report))
end)
