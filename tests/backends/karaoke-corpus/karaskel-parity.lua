-- Reports what karaskel's collect_head and preproc_line leave in their tables, from karaskel.lua's own Lua or
-- routed to the helper's native builder, as text that is equal when the two are equal field for field.
script_name = "karaskel parity"

include("karaskel.lua")

local function dump(root)
	local seen, out = {}, {}
	local function name(k)
		return type(k) == "number" and string.format("[%.17g]", k) or "." .. tostring(k)
	end
	local function before(a, b)
		if type(a) ~= type(b) then
			return type(a) < type(b)
		end
		return a < b
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
			local keys = {}
			for k in pairs(v) do
				table.insert(keys, k)
			end
			table.sort(keys, before)
			for _, k in ipairs(keys) do
				walk(v[k], path .. name(k))
			end
		elseif kind == "number" then
			table.insert(out, path .. " = " .. string.format("%.17g", v))
		else
			table.insert(out, path .. " = " .. kind .. " " .. tostring(v))
		end
	end
	walk(root, "")
	return table.concat(out, "\n")
end

local function json_string(s)
	return '"' .. s:gsub('[%c"\\]', function(c)
		return string.format("\\u%04x", c:byte())
	end) .. '"'
end

local function run(native)
	return function(subs)
		local impl = aegisub.__karaskel_impl
		local own_head, own_line = impl.collect_head, impl.preproc_line
		local ran = { head = false, line = 0 }
		impl.collect_head = function(...)
			local meta, styles = own_head(...)
			ran.head = meta ~= nil
			return meta, styles
		end
		impl.preproc_line = function(...)
			local done = own_line(...)
			ran.line = ran.line + (done and 1 or 0)
			return done
		end
		if not native then
			aegisub.__karaskel_impl = nil
		end
		local meta, styles = karaskel.collect_head(subs, true)
		local parts = { dump({ meta = meta, styles = styles }) }
		for i = 1, #subs do
			local line = subs[i]
			if line.class == "dialogue" then
				karaskel.preproc_line(subs, meta, styles, line)
				table.insert(parts, dump(line))
			end
		end
		aegisub.__karaskel_impl = impl
		impl.collect_head, impl.preproc_line = own_head, own_line
		aegisub.hikari.report('{"dump":' .. json_string(table.concat(parts, "\n--\n")) .. ',"native_head":'
			.. tostring(ran.head) .. ',"native_lines":' .. ran.line .. "}")
	end
end

aegisub.register_macro("Lua karaskel", "", run(false))
aegisub.register_macro("Native karaskel", "", run(true))
