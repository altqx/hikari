-- DC1 fixture: DependencyControl installs a module from a feed and updates
-- it, with the calls its Toolbox makes. The feed's URL and the version to
-- update to come from the environment.
script_name = "DependencyControl feed"
script_namespace = "test.DepCtrlFeed"

local DepCtrl = require("l0.DependencyControl")
local UpdateTask = DepCtrl.UpdateTask

local function run(record, target)
    local task, status, result = DepCtrl.updater:addTask(record, target, nil, nil, "stable",
        UpdateTask.UpdateReason.UserRequested)
    if task then
        status, result = task:run()
    end
    DepCtrl.updater:releaseLock()
    return tostring(status) .. "," .. tostring(result)
end

aegisub.register_macro("Install", "", function()
    aegisub.log("%s", run({namespace = "test.Greeter", moduleName = "test.Greeter", name = "Greeter",
        feed = os.getenv("HIKARI_TEST_FEED"), virtual = true}))
end)

aegisub.register_macro("Update", "", function()
    local result = run(require("test.Greeter").version, os.getenv("HIKARI_TEST_VERSION"))
    aegisub.log("%s,%s", result, require("test.Greeter").greet())
end)
