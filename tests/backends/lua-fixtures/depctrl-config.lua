-- DC1 fixture: DependencyControl's config as scripts use it.
-- Show: a script written for DependencyControl < 0.7 tells its
-- getConfigHandler result from a dialog's result table by its class, as
-- lyger's FbfTransform and GradientEverything do in save_preset.
-- Writers: two handlers on one file, as two scripts' helpers have; one
-- adds a key, and the other's later saves keep it.
-- Migrate: another helper saves between a handler's first read and its
-- schema migration; the migrated file keeps what it saved.
script_name = "DependencyControl config"
script_description = ""
script_version = "0.0.1"
script_author = "test"
script_namespace = "test.DepCtrlConfig"

local DepCtrl = require("l0.DependencyControl")
local ConfigHandler = require("l0.DependencyControl.ConfigHandler")
local rec = DepCtrl({})
local preset = rec:getConfigHandler({presets = {}}, {"presets", "Default"})

aegisub.register_macro("Show", "", function()
    aegisub.log("%s,%s", tostring(preset.__class == DepCtrl.ConfigHandler),
        tostring(({}).__class == DepCtrl.ConfigHandler))
end)

aegisub.register_macro("Writers", "", function()
    local path = aegisub.decode_path("?user/config/test.Writers.json")
    local mine = ConfigHandler(path)
    assert(mine:load())
    local view = mine:getView({"package"})
    view.c.mine = 1
    assert(view:save())
    local theirs = ConfigHandler(path)
    assert(theirs:load())
    local other = theirs:getView({"package"})
    other.c.theirs = 1
    assert(other:save())
    for i = 2, 3 do
        view.c.mine = i
        assert(view:save())
    end
    local check = ConfigHandler(path)
    assert(check:load())
    local hive = check:getView({"package"}).c
    aegisub.log("%s,%s", tostring(hive.mine), tostring(hive.theirs))
end)

aegisub.register_macro("Migrate", "", function()
    local path = aegisub.decode_path("?user/config/test.Migrate.json")
    local f = assert(io.open(path, "w"))
    f:write('{"package": {"old": 1}}')
    f:close()
    local handler = ConfigHandler(path, nil, {schemaId = "test-2", migrate = function(config)
        config.package.migrated = true
        return true
    end})
    local read, first = handler.__readFile, true
    handler.__readFile = function(self, ...)
        local config, err = read(self, ...)
        if first then
            first = false
            local other = assert(io.open(path, "w"))
            other:write('{"package": {"old": 1, "theirs": 1}}')
            other:close()
        end
        return config, err
    end
    assert(handler:load())
    local check = ConfigHandler(path)
    assert(check:load())
    local hive = check:getView({"package"}).c
    aegisub.log("%s,%s,%s", tostring(hive.migrated), tostring(hive.theirs), tostring(check.schemaId))
end)
