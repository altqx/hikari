-- Writes the shape kernel's goldens from the shipped Yutils.lua:
--   cd Automation/automation/Include && luajit <repo>/tests/fixtures/shape-kernel/make_goldens.lua > <repo>/tests/fixtures/shape-kernel/yutils-goldens.txt
-- Record format, one field a line: "case <function> [arguments]", "in <shape>", then "out <result>"
-- (several for to_pixels, one a pixel) or "error"; "end" closes the record. Backslashes and newlines
-- in shapes are written as \\ and \n.
local Yutils = require("Yutils")

local seed = 20261112
local function rand(n)
	seed = (seed * 1103515245 + 12345) % 2147483648
	return seed % n
end

local function coord(spread)
	local v = rand(spread * 2000) / 1000 - spread
	if rand(3) == 0 then
		v = math.floor(v)
	end
	return v
end

local function random_shape(curves, spread)
	spread = spread or 60
	local function c()
		return coord(spread)
	end
	local parts, figures = {}, 1 + rand(3)
	for f = 1, figures do
		parts[#parts + 1] = string.format("m %s %s", c(), c())
		for _ = 1, 2 + rand(5) do
			if curves and rand(2) == 0 then
				parts[#parts + 1] = string.format("b %s %s %s %s %s %s", c(), c(), c(), c(), c(), c())
			else
				parts[#parts + 1] = string.format("l %s %s", c(), c())
			end
		end
	end
	return table.concat(parts, " ")
end

local handmade = {
	"m 0 0 l 100 0 100 100 0 100",
	"m 0 0 b 10 0 20 10 20 20",
	"m 10 10 l 30 10 b 40 10 40 30 30 30 l 10 30",
	"m -5.5 -3.25 l 12.125 0 4 9.875",
	"m 0 0 l 20 0 20 20 0 20 m 5 5 l 15 5 15 15 5 15",
	"m 0 0 b 0 -50 100 -50 100 0 b 100 50 0 50 0 0",
	"m 0 0 l 50 0 50 0 50 50 0 50 0 0",
	"m   1   2  l 3 4\t5 6\n7 8",
	"m 0 0 l 1 1 b 2 2 3 3",
	"m 0 0 l 30 0 15 26",
	"m 0 0 l 10 0 10 10 b 5 15 0 10 0 10",
	"m 0.0004 0.0006 l 10.9996 0.0005 10.5 10.4995",
	"m 0 0 l 100 0 0 1",
	"m 0 0 xb 1 2 b 3 4 5 6 7 8",
	"m 0 0 b 1 2 3 4 5 6 7 8 l 9 9",
	"m 0 0 s 10 0 10 10 0 10 c",
	"",
	"m",
	"m 5 5",
}

local shapes, line_shapes = {}, {}
for _, s in ipairs(handmade) do
	shapes[#shapes + 1] = s
end
for _ = 1, 40 do
	shapes[#shapes + 1] = random_shape(true, 12)
end
for _ = 1, 20 do
	shapes[#shapes + 1] = random_shape(false, 60)
end
for i, s in ipairs(shapes) do
	local ok, flat = pcall(Yutils.shape.flatten, s)
	if ok and (i <= #handmade or i % 4 == 0) then
		line_shapes[#line_shapes + 1] = flat
	end
end
for _ = 1, 40 do
	line_shapes[#line_shapes + 1] = random_shape(false)
end

local function g(x)
	return string.format("%.17g", x)
end

local function escape(text)
	return (text:gsub("\\", "\\\\"):gsub("\n", "\\n"))
end

local function record(head, shape, fn)
	io.write("case ", head, "\n", "in ", escape(shape), "\n")
	local results = {pcall(fn)}
	if not results[1] then
		io.write("error\n")
	else
		local first = results[2]
		if type(first) == "string" then
			io.write("out ", escape(first), "\n")
		elseif type(first) == "table" then
			for _, p in ipairs(first) do
				io.write("out ", g(p.alpha), " ", g(p.x), " ", g(p.y), "\n")
			end
		elseif first == nil then
			io.write("out nil\n")
		else
			io.write("out ", g(results[2]), " ", g(results[3]), " ", g(results[4]), " ", g(results[5]), "\n")
		end
	end
	io.write("end\n")
end

for i, s in ipairs(shapes) do
	record("bounding", s, function() return Yutils.shape.bounding(s) end)
	record("flatten 1", s, function() return Yutils.shape.flatten(s) end)
	if i <= #handmade then
		record("flatten 0.25", s, function() return Yutils.shape.flatten(s, 0.25) end)
	end
	local dx, dy = coord(40), coord(40)
	record("move " .. g(dx) .. " " .. g(dy), s, function() return Yutils.shape.move(s, dx, dy) end)
	local m = Yutils.math.create_matrix()
	m.translate(coord(30), coord(30), 0).scale(1 + rand(300) / 100, 1 + rand(300) / 100, 1).rotate("z", rand(360))
	if rand(2) == 0 then
		m.rotate("x", rand(60))
	end
	local data = m.get_data()
	local fields = {}
	for i = 1, 16 do
		fields[i] = g(data[i])
	end
	record("transform " .. table.concat(fields, " "), s, function() return Yutils.shape.transform(s, m) end)
end
local pixel_shapes = {"m 0 0 l 4 0 4 4 0 4", "m 0.5 0.5 l 3.5 0.5 3.5 3.5", "m -2.25 1 b 0 -3 3 -3 5.5 1", "m 0 0 l 6 0 6 6 0 6 m 2 2 l 2 4 4 4 4 2",
	"m 0 0 l 0.0625 0 0.0625 0.0625", "m 3 3 l 0 0 6 0", "", "m 1 1"}
for _ = 1, 60 do
	pixel_shapes[#pixel_shapes + 1] = random_shape(true, 10)
end
for _, s in ipairs(pixel_shapes) do
	record("to_pixels", s, function() return Yutils.shape.to_pixels(s) end)
end
local modes = {"round", "bevel", "miter"}
for i, s in ipairs(line_shapes) do
	local mode = modes[1 + i % 3]
	local w = 0.5 + rand(80) / 10
	record("to_outline " .. g(w) .. " - " .. mode, s, function() return Yutils.shape.to_outline(s, w, nil, mode) end)
	if i % 4 == 0 then
		local wy = rand(60) / 10
		record("to_outline " .. g(w) .. " " .. g(wy) .. " " .. mode, s,
			function() return Yutils.shape.to_outline(s, w, wy, mode) end)
	end
end
record("to_outline 2 - round", "m 0 0 l 10 0 b 10 10 0 10 0 0", function() return Yutils.shape.to_outline("m 0 0 l 10 0 b 10 10 0 10 0 0", 2) end)
record("to_outline 0 - round", "m 0 0 l 10 0 10 10", function() return Yutils.shape.to_outline("m 0 0 l 10 0 10 10", 0) end)
