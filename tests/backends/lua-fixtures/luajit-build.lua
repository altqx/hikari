-- L6 fixture: the LuaJIT build's Lua 5.2 extensions and string.buffer, as
-- each legacy platform had them (R5-per-platform): the legacy Windows build
-- defines LUAJIT_ENABLE_LUA52COMPAT and LUAJIT_DISABLE_BUFFER
-- (Thirdparty/Build/LuaJit/LuaJit.vcxproj); the legacy Linux package used its
-- distribution's LuaJIT, built with neither.
script_name = "LuaJIT build"

aegisub.register_macro("Show", "", function(subs)
    local ipairs_meta = false
    pcall(function()
        for _ in ipairs(setmetatable({}, { __ipairs = function() ipairs_meta = true; return function() end end })) do end
    end)
    -- The subtitles object is userdata with __ipairs, as in legacy.
    local subs_ipairs = pcall(function() for _ in ipairs(subs) do end end)
    aegisub.log("%s,%s,%s,%s,%s", type(table.pack), tostring(ipairs_meta),
        tostring(loadstring("repeat break; local x = 1 until true") ~= nil),
        tostring((pcall(require, "string.buffer"))), tostring(subs_ipairs))
end)
