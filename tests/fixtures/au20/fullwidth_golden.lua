local root, inputs, output = arg[1], arg[2], arg[3]
local macro
aegisub = {
	gettext = function(s) return s end,
	debug = { out = function() end },
	set_undo_point = function() end,
	register_macro = function(_, _, f) macro = f end,
}
local function charwidth(s, i)
	local b = s:byte(i or 1)
	if not b or b < 128 then return 1 elseif b < 224 then return 2 elseif b < 240 then return 3 else return 4 end
end
package.preload["aegisub.unicode"] = function()
	return {
		charwidth = charwidth,
		chars = function(s)
			local curchar, i = 0, 1
			return function()
				if i > #s then return end
				local j = i
				curchar = curchar + 1
				i = i + charwidth(s, i)
				return s:sub(j, i - 1), curchar
			end
		end,
	}
end
function include(name) dofile(root .. "/Automation/automation/Include/" .. name) end
dofile(root .. "/Automation/automation/Autoload/macro-2-mkfullwitdh.lua")
local subs, sel = {}, {}
for line in io.lines(inputs) do
	subs[#subs + 1] = { text = line }
	sel[#sel + 1] = #subs
end
local originals = {}
for i, l in ipairs(subs) do originals[i] = l.text end
macro(subs, sel)
local f = io.open(output, "wb")
for i, l in ipairs(subs) do f:write(originals[i], "\t", l.text, "\n") end
f:close()
