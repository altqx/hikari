-- #402 fixture: where DependencyControl finds a bundled module's entry point,
-- which decides whether it may update it: the user folder's lower-case
-- include, else the program's Include (?data: a protected install).
script_name = "DependencyControl entry point"

require("l0.DependencyControl")
local PackageRecord = require("l0.DependencyControl.PackageRecord")
local domain = require("l0.DependencyControl.domain")

aegisub.register_macro("Find", "", function()
    local record = {scriptType = domain.ScriptType.Module, namespace = "l0.Functional",
        getPossibleEntryPointPaths = PackageRecord.getPossibleEntryPointPaths}
    local path, isUserPath = PackageRecord.getEntryPointPath(record)
    aegisub.log("%s,%s", tostring(isUserPath), tostring(path))
end)
