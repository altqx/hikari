-- DC1 fixture: DependencyControl installs a module from a feed and updates
-- it, with the calls its Toolbox makes. The feed's URL, the version to
-- update to and a feed to add to the update come from the environment.
-- Direct loads the feed through an UpdateFeed built without FeedLoader.
script_name = "DependencyControl feed"
script_namespace = "test.DepCtrlFeed"

local DepCtrl = require("l0.DependencyControl")
local UpdateTask = DepCtrl.UpdateTask

local function run(record, target)
    local task, status, result = DepCtrl.updater:addTask(record, target, {os.getenv("HIKARI_TEST_ADD_FEED")}, nil, "stable",
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

aegisub.register_macro("Direct", "", function()
    local feed = DepCtrl.UpdateFeed(os.getenv("HIKARI_TEST_FEED"), false, nil, {blockPrivateHosts = false})
    local data, err = feed:ensureLoaded()
    aegisub.log("%s", data and data.name or tostring(err))
end)
