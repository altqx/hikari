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

local function clusters(unicode, s)
    local t = {}
    for cluster in unicode.graphemes(s) do t[#t + 1] = cluster end
    return table.concat(t, "|")
end

local function cluster_lengths(unicode, s)
    local t = {}
    for cluster in unicode.graphemes(s) do t[#t + 1] = #cluster end
    return table.concat(t, " ")
end

aegisub.register_macro("Graphemes", "", function()
    local unicode = require 'aegisub.unicode'
    show(clusters(unicode, "กินน้ำ"), clusters(unicode, "e\204\129a"), clusters(unicode, ""))
    show(clusters(unicode, "👨‍👩‍👧!"), clusters(unicode, "🇹🇭🇯🇵🇹"))
    show(cluster_lengths(unicode, "a\r\nb"), cluster_lengths(unicode, "a\255"), cluster_lengths(unicode, "ก\224\184"))
    local _, n = unicode.graphemes("ab")()
    show(n)
end)

aegisub.register_macro("GraphemesFallback", "", function()
    local impl = require 'aegisub.__unicode_impl'
    local native, loaded = impl.grapheme_end, package.loaded['aegisub.unicode']
    impl.grapheme_end, package.loaded['aegisub.unicode'] = nil, nil
    local ok, fallback = pcall(require, 'aegisub.unicode')
    impl.grapheme_end, package.loaded['aegisub.unicode'] = native, loaded
    assert(ok, fallback)
    show(fallback ~= loaded, clusters(fallback, "กินน้ำ"), clusters(fallback, "e\204\129a"))
    show(clusters(fallback, "👨‍👩‍👧!"), cluster_lengths(fallback, "ก\224\184"))
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
    local moved, why = lfs.chdir(dir .. "/missing")
    show(select('#', lfs.chdir(dir .. "/missing")), moved, type(why))
    local ok, err = pcall(lfs.dir, dir .. "/missing")
    show(ok, type(err))
    os.remove(dir .. "/a.txt")
    show(lfs.rmdir(dir))
end)

aegisub.register_macro("Util", "", function()
    local util = require 'aegisub.util'
    local t = { a = { 1 } }
    t.self = t
    t.a.back = t
    local c = util.deep_copy(t)
    show(c ~= t, c.self == c, c.a.back == c, c.a ~= t.a, c.a[1])
end)
