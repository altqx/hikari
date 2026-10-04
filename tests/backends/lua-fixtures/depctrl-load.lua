-- L6 fixture: DependencyControl required by a script's top level, while the
-- host loads it (legacy capture: it loads there as in a macro).
script_name = "DependencyControl at load"

local ok, mod = pcall(require, "l0.DependencyControl")
local atLoad = string.format("%s,%s,%s", tostring(ok), type(mod), ok and "" or tostring(mod):match("[^\n]*"))

aegisub.register_macro("Show", "", function()
    aegisub.log("%s", atLoad)
end)
