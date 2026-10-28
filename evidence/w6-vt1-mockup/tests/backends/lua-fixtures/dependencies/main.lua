-- S4-run-validation fixture: the files a script's top level loads as Lua
-- code (require and include, and what those load) are its dependencies'
-- candidates; ScriptInfo::loadedFiles lists them, first load first, once.
script_name = "Depends"

aegisub.register_macro("Main", "", function(subs, sel)
    -- Loaded while a macro runs, not by the top level: not listed.
    require "lazy"
end)

local dep = require "dep"
require "dep" -- loaded once, listed once
include("included.lua")
