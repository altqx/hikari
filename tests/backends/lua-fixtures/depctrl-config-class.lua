-- DC1 fixture: a script written for DependencyControl < 0.7 tells its
-- getConfigHandler result from a dialog's result table by its class, as
-- lyger's FbfTransform and GradientEverything do in save_preset.
script_name = "DependencyControl config class"
script_description = ""
script_version = "0.0.1"
script_author = "test"
script_namespace = "test.DepCtrlConfigClass"

local DepCtrl = require("l0.DependencyControl")
local rec = DepCtrl({})
local preset = rec:getConfigHandler({presets = {}}, {"presets", "Default"})

aegisub.register_macro("Show", "", function()
    aegisub.log("%s,%s", tostring(preset.__class == DepCtrl.ConfigHandler),
        tostring(({}).__class == DepCtrl.ConfigHandler))
end)
