-- Calls aegisub.util with and without the helper's functions on the same inputs and reports any difference in
-- what they return or raise.
script_name = "util parity"

local util = require("aegisub.util")

local function shown(...)
	local out = { tostring(select("#", ...)) }
	for i = 1, select("#", ...) do
		local v = select(i, ...)
		table.insert(out, type(v) == "number" and string.format("%.17g", v) or type(v) .. ":" .. tostring(v))
	end
	return table.concat(out, " ")
end

local function outcome(f, ...)
	local result = { pcall(f, ...) }
	if not result[1] then
		return "error " .. tostring(result[2])
	end
	return shown(unpack(result, 2, table.maxn(result)))
end

local function words_of(s)
	local out = {}
	for w in util.words(s) do
		table.insert(out, w)
	end
	return unpack(out)
end

local seed = 12345
local function random(n)
	seed = (seed * 1103515245 + 12345) % 2147483648
	return seed % n
end

local alphabet = "&H#&H#0123456789abcdefABCDEF0f9Fgxyz.- \t\n\v\f\r\0"
local function random_text()
	local out = {}
	for _ = 1, random(16) do
		local i = random(#alphabet) + 1
		table.insert(out, alphabet:sub(i, i))
	end
	return table.concat(out)
end

local texts = { "", " ", "&H00FF8040", "&H80102030&", "&HFF&", "#FF8040", "#F", "#FF8", "#FF80401", "#ff8040AA",
	"x&H12345678y", "&H1234567&H00000000", "&H123456&&H12&", "&HZZ&H12&", "  ka ra  ", "\tok\n", "no space",
	"a\0b c", "\v\f\r", "&h00ff8040", "&H00FF80&", "#", "##12", " \t" }
for _ = 1, 3000 do
	table.insert(texts, random_text())
end
local numbers = { 0, -0.0, 1, -1, 0.5, 0.25, 1 / 3, 2 / 3, 1 / 6, 359.999, 360, 720.5, -45, 1e300, -1e300, 1 / 0,
	-1 / 0, 0 / 0, 1e-300, 0.49999999999999994, 0.5000000000000001 }
for _ = 1, 400 do
	table.insert(numbers, (random(200001) - 100000) / (random(997) + 1))
end
local others = { true, 12, { 1 }, "12", "0x10" }

local checks = {
	{ "extract_color", function(s) return util.extract_color(s) end },
	{ "trim", function(s) return util.trim(s) end },
	{ "headtail", function(s) return util.headtail(s) end },
	{ "words", words_of },
	{ "alpha_from_style", function(s) return util.alpha_from_style(s) end },
	{ "color_from_style", function(s) return util.color_from_style(s) end },
	{ "interpolate_color", function(s) return util.interpolate_color(0.3, s, "&H00FF8040") end },
	{ "interpolate_alpha", function(s) return util.interpolate_alpha(0.7, "&H80102030&", s) end },
}

aegisub.register_macro("Compare", "", function()
	local impl = aegisub.__util_impl
	local mismatches, compared = {}, 0
	local function compare(label, f, ...)
		local native = outcome(f, ...)
		aegisub.__util_impl = nil
		local lua = outcome(f, ...)
		aegisub.__util_impl = impl
		compared = compared + 1
		if native ~= lua and #mismatches < 20 then
			table.insert(mismatches, label .. " " .. shown(...) .. ": " .. native .. " | " .. lua)
		end
	end
	for _, check in ipairs(checks) do
		for _, s in ipairs(texts) do
			compare(check[1], check[2], s)
		end
		for i = 1, #others + 1 do
			compare(check[1], check[2], others[i])
		end
	end
	for _, h in ipairs(numbers) do
		for _, s in ipairs({ 0, 1, 0.5, 0.3, -0.2, 1.7, 0 / 0, numbers[random(#numbers) + 1] }) do
			for _, l in ipairs({ 0, 1, 0.25, 0.5, 0.75, -1, 2, 0 / 0, numbers[random(#numbers) + 1] }) do
				compare("HSL_to_RGB", util.HSL_to_RGB, h, s, l)
			end
		end
	end
	for _, v in ipairs({ "10", "x", true }) do
		compare("HSL_to_RGB", util.HSL_to_RGB, v, 0.5, 0.5)
		compare("HSL_to_RGB", util.HSL_to_RGB, 10, v, 0.5)
		compare("HSL_to_RGB", util.HSL_to_RGB, 10, 0.5, v)
	end
	local json = {}
	for _, m in ipairs(mismatches) do
		table.insert(json, '"' .. m:gsub('[%c"\\]', function(c) return string.format("\\u%04x", c:byte()) end) .. '"')
	end
	aegisub.hikari.report('{"compared":' .. compared .. ',"native":' .. tostring(impl ~= nil) .. ',"mismatches":['
		.. table.concat(json, ",") .. "]}")
end)
