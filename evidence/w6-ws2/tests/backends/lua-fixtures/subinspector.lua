-- L6 fixture (A33-subinspector-linux): ASSFoundation's SubInspector module
-- loads its native library and measures a rendered line.
script_name = "SubInspector"

aegisub.register_macro("Bounds", "", function(subs, sel)
    local ok, Inspector = pcall(require, "SubInspector.Inspector")
    if not ok then return aegisub.log("require failed: %s", tostring(Inspector)) end
    local inspector = Inspector(subs)
    local line = subs[sel[1]]
    local bounds, times = inspector:getBounds({ line })
    if bounds == nil then return aegisub.log("getBounds failed: %s", tostring(times)) end
    local b = bounds[1]
    aegisub.log("%d,%s,%s", #bounds, tostring(b ~= false and b.w > 0 and b.h > 0),
        tostring(b ~= false and b.x >= 0 and b.y >= 0))
end)
