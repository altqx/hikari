-- Reports what karaskel's collect_head and preproc_line leave in their tables, from karaskel.lua or from
-- the helper's native builder, as text that is equal when the two are equal field for field.
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
		local impl = native and aegisub.__karaskel_impl or karaskel
		local meta, styles = impl.collect_head(subs, true)
		local parts = { dump({ meta = meta, styles = styles }) }
		for i = 1, #subs do
			local line = subs[i]
			if line.class == "dialogue" then
				impl.preproc_line(subs, meta, styles, line)
				table.insert(parts, dump(line))
			end
		end
		aegisub.hikari.report('{"dump":' .. json_string(table.concat(parts, "\n--\n")) .. "}")
	end
end

aegisub.register_macro("Lua karaskel", "", run(false))
aegisub.register_macro("Native karaskel", "", run(true))
