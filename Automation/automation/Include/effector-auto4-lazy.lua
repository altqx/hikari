local loaded = false
local bound = false
local stand_ins = {}
local stood_in = {}
local bindings = {}

local function bind_as(kind, get, names, once)
	bindings[#bindings + 1] = {kind = kind, get = get, names = names, once = once}
end

local function made(name)
	return function(_, set)
		if set[name] ~= nil then
			return set[name]
		end
		return rawget(_G, name)
	end
end

local function math_function(name)
	return function(ke4) return ke4.math[name] end
end

bind_as('table', function(ke4) return ke4 end, {'ke4'})
bind_as('table', made 'temp', {'temp'}, true)
bind_as('table', made 'recall', {'recall'}, true)
for _, names in ipairs {{'R'}, {'Rs'}, {'Rd'}, {'Rc'}, {'Rm'}, {'Rds', 'Rsd'}, {'Rcs', 'Rsc'}, {'Rms', 'Rsm'},
	{'Rr'}, {'Rsr', 'Rrs'}, {'Rdr'}, {'Rcr'}, {'Rmr'}, {'Rdrs'}, {'Rcrs'}, {'Rmrs'}, {'Re'}} do
	bind_as('function', math_function(names[1]), names)
end
bind_as('function', function(ke4) return ke4.tag.ipol end, {'interpolate', 'interpolate_alpha', 'interpolate_color'})

local function load()
	if loaded then
		return
	end
	local before = {}
	for k, v in pairs(_G) do
		before[k] = v
	end
	local ke4 = require 'effector-auto4'
	local set = {}
	for k, v in pairs(_G) do
		local old = before[k]
		if old ~= v then
			set[k] = v
			if (old ~= nil or stood_in[k]) and not stand_ins[old] then
				rawset(_G, k, old)
			end
		end
	end
	for _, b in ipairs(bindings) do
		b.value = b.get(ke4, set)
	end
	loaded = true
	for _, b in ipairs(bindings) do
		for _, name in ipairs(b.names) do
			if rawget(_G, name) == b.stand_in then
				rawset(_G, name, b.value)
			end
		end
	end
end

for _, b in ipairs(bindings) do
	if b.kind == 'function' then
		b.stand_in = function(...)
			load()
			return b.value(...)
		end
	else
		b.stand_in = setmetatable({}, {
			__index = function(_, k)
				load()
				return b.value[k]
			end,
			__newindex = function(_, k, v)
				load()
				b.value[k] = v
			end,
		})
	end
	stand_ins[b.stand_in] = true
end

return function()
	if package.loaded['effector-auto4'] ~= nil then
		load()
	end
	for _, b in ipairs(bindings) do
		if not b.once or not bound and not loaded then
			for _, name in ipairs(b.names) do
				_G[name] = loaded and b.value or b.stand_in
				stood_in[name] = not loaded or nil
			end
		end
	end
	bound = true
end
