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
