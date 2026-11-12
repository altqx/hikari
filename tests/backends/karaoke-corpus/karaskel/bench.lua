-- Times karaskel's collect_head and preproc_line in its own Lua and routed to the helper, JIT-warm.
script_name = "karaskel bench"

include("karaskel.lua")

local clock = aegisub.hikari.clock

local function copy(t)
	local out = {}
	for k, v in pairs(t) do
		out[k] = v
	end
	return out
end

local function each_round(rounds, f)
	for _ = 1, math.max(1, math.floor(rounds / 5)) do
		f()
	end
	local start = clock()
	for _ = 1, rounds do
		f()
	end
	return (clock() - start) / rounds
end

local function lua_only(f)
	return function()
		local impl = aegisub.__karaskel_impl
		aegisub.__karaskel_impl = nil
		f()
		aegisub.__karaskel_impl = impl
	end
end

aegisub.register_macro("Time karaskel", "", function(subs)
	local rounds = 200
	local meta, styles = karaskel.collect_head(subs, true)
	local lines = {}
	for i = 1, #subs do
		local l = subs[i]
		if l.class == "dialogue" then
			table.insert(lines, l)
		end
	end
	local function over_lines(f)
		return function()
			for _, l in ipairs(lines) do
				f(copy(l))
			end
		end
	end
	local copying = each_round(rounds, over_lines(function() end))
	local lua = each_round(rounds, lua_only(over_lines(function(l) karaskel.preproc_line(subs, meta, styles, l) end)))
	local routed = each_round(rounds, over_lines(function(l) karaskel.preproc_line(subs, meta, styles, l) end))
	local head_rounds = math.max(1, math.floor(rounds / 4))
	local head_lua = each_round(head_rounds, lua_only(function() karaskel.collect_head(subs, true) end))
	local head_routed = each_round(head_rounds, function() karaskel.collect_head(subs, true) end)
	aegisub.hikari.report(string.format(
		'{"lines":%d,"lua_line_us":%.3f,"routed_line_us":%.3f,"lua_head_us":%.3f,"routed_head_us":%.3f}', #lines,
		(lua - copying) / #lines * 1e6, (routed - copying) / #lines * 1e6, head_lua * 1e6, head_routed * 1e6))
end)

aegisub.register_macro("Time util", "", function()
	local util = require("aegisub.util")
	local impl = aegisub.__util_impl
	local N = 200000
	local loop_source = [[
		local f, n, clock = ...
		for i = 1, math.floor(n / 10) do f(i) end
		local start = clock()
		for i = 1, n do f(i) end
		return (clock() - start) / n * 1e9
	]]
	local function time(f)
		return loadstring(loop_source)(f, N, clock)
	end
	local sink = 0
	local colours = { "&H00FF8040", "&H80102030&", "&HFFEEDDCC" }
	local texts = { "  ka ra  ", "\tok", "no space" }
	local small = { a = 1, b = 2, c = 3, d = "x", e = "y", [1] = 1, [2] = 2, [3] = 3 }
	local nested = { a = { b = { c = 1 }, d = { 1, 2, 3 } }, e = small }
	local cases = {
		{ "copy", function() sink = sink + #util.copy(small) end },
		{ "deep_copy", function() sink = sink + #util.deep_copy(nested) end },
		{ "ass_color", function(i) sink = sink + #util.ass_color(i % 256, 20, 30) end },
		{ "ass_alpha", function(i) sink = sink + #util.ass_alpha(i % 256) end },
		{ "ass_style_color", function(i) sink = sink + #util.ass_style_color(i % 256, 1, 2, 3) end },
		{ "extract_color", function(i) sink = sink + util.extract_color(colours[i % 3 + 1]) end },
		{ "alpha_from_style", function(i) sink = sink + #util.alpha_from_style(colours[i % 3 + 1]) end },
		{ "color_from_style", function(i) sink = sink + #util.color_from_style(colours[i % 3 + 1]) end },
		{ "HSV_to_RGB", function(i) sink = sink + util.HSV_to_RGB(i % 360, 0.5, 0.4) end },
		{ "HSL_to_RGB", function(i) sink = sink + util.HSL_to_RGB(i % 360, 0.5, 0.4) end },
		{ "trim", function(i) sink = sink + #util.trim(texts[i % 3 + 1]) end },
		{ "headtail", function(i) sink = sink + #util.headtail(texts[i % 3 + 1]) end },
		{ "words", function(i) for w in util.words(texts[i % 3 + 1]) do sink = sink + #w end end },
		{ "clamp", function(i) sink = sink + util.clamp(i % 7, 2, 5) end },
		{ "interpolate", function(i) sink = sink + util.interpolate((i % 10) / 10, 0, 100) end },
		{ "interpolate_color", function(i) sink = sink + #util.interpolate_color((i % 10) / 10, colours[1], colours[3]) end },
		{ "interpolate_alpha", function(i) sink = sink + #util.interpolate_alpha((i % 10) / 10, colours[1], colours[3]) end },
	}
	local rows = {}
	for _, case in ipairs(cases) do
		aegisub.__util_impl = nil
		local lua = time(case[2])
		aegisub.__util_impl = impl
		local routed = time(case[2])
		table.insert(rows, string.format('{"name":"%s","lua_ns":%.2f,"routed_ns":%.2f}', case[1], lua, routed))
	end
	aegisub.hikari.report('{"sink":' .. sink .. ',"rows":[' .. table.concat(rows, ",") .. "]}")
end)
