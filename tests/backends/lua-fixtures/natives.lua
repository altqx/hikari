-- L6 fixture: the legacy native preloads through the bundled wrappers.
script_name = "Natives"

local function show(...)
    local t = {}
    for i = 1, select('#', ...) do t[#t + 1] = tostring((select(i, ...))) end
    aegisub.log("%s", table.concat(t, ","))
end

aegisub.register_macro("Regex", "", function()
    local re = require 'aegisub.re'
    local m = re.match("Hello World", "(\\w+) (\\w+)")
    show(m[1].str, m[2].str, m[3].str, m[2].first, m[2].last)
    show(re.sub("a1b22c333", "\\d+", "#"))
    show(re.sub("a1b22c333", "\\d+", "#", 2))
    show(re.find("Ärger ärger", "ärger", re.ICASE) ~= nil)
    show(#re.split("a,b,,c", ","))
    local ok, err = pcall(re.compile, "(")
    show(ok, err ~= nil)
end)

-- re.moon reads a search result and then frees it; once traced, the reads must
-- not be fused past the free (the Hikari LuaJIT overlay patch).
aegisub.register_macro("RegexTraced", "", function()
    local re = require 'aegisub.re'
    local wrong = 0
    for _ = 1, 3000 do
        if #re.split("a,b,,c", ",") ~= 4 then wrong = wrong + 1 end
    end
    show(wrong)
end)

aegisub.register_macro("Unicode", "", function()
    local unicode = require 'aegisub.unicode'
    show(unicode.to_upper_case("straße"), unicode.to_lower_case("ÀÉÎ"), unicode.to_fold_case("Straße"))
    show(unicode.len("日本語"), unicode.codepoint("日"))
end)

aegisub.register_macro("Lpeg", "", function()
    local lpeg = require 'lpeg'
    show(lpeg.match(lpeg.P"a"^1, "aaab"), lpeg.version())
end)

aegisub.register_macro("Luabins", "", function()
    local luabins = require 'luabins'
    local saved = luabins.save(1, "two", {3})
    local ok, a, b, c = luabins.load(saved)
    show(ok, a, b, c[1])
end)

aegisub.register_macro("Lfs", "", function(subs)
    local lfs = require 'aegisub.lfs'
    local base = aegisub.decode_path("?temp")
    local dir = base .. "/hikari-lfs"
    show(lfs.mkdir(dir))
    show(lfs.attributes(dir, "mode"))
    show(lfs.touch(dir .. "/a.txt"))
    show(lfs.attributes(dir .. "/a.txt", "mode"), lfs.attributes(dir .. "/a.txt", "size"))
    local names = {}
    for name in lfs.dir(dir) do names[#names + 1] = name end
    table.sort(names)
    show(table.concat(names, " "))
    show(lfs.attributes(dir .. "/missing", "mode"))
    os.remove(dir .. "/a.txt")
    show(lfs.rmdir(dir))
end)
