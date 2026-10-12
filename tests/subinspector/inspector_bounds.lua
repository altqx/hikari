-- Loads SubInspector.Inspector from a package's automation Include directory
-- the way HikariSub's AutomationScriptReader Install() and module_loader do,
-- then measures sample lines through the packaged libSubInspector.so.
--
-- Usage: luajit inspector_bounds.lua <Include dir> <lpeg module dir> <font dir> <libSubInspector.so>
-- Run it through check-package.sh, which builds the lpeg module.
local include, lpegdir, fontdir, library = arg[1], arg[2], arg[3], arg[4]
assert(include and lpegdir and fontdir and library,
  "usage: inspector_bounds.lua <Include dir> <lpeg module dir> <font dir> <libSubInspector.so>")

-- The app adds "<dir>/?.lua;<dir>/?/init.lua;" per include directory, and its
-- include directories end in a separator.
local dir = include .. "/"
package.path = dir .. "/?.lua;" .. dir .. "/?/init.lua;"
-- The app links LPeg in; here it is a C module beside the test.
package.cpath = lpegdir .. "/?.so"

local moonloadstring
-- module_loader: a .moon file beside the .lua path wins, a UTF-8 BOM is
-- dropped, and it replaces Lua's own path loader (package.loaders[2]).
package.loaders[2] = function(name)
  local module = name:gsub("%.", "/")
  for template in package.path:gmatch("[^;]+") do
    local filename = template:gsub("/%?", module, 1)
    if filename:match("lua$") then
      local moonpath = filename:gsub("%.[^.]*$", ".moon")
      local f = io.open(moonpath, "rb")
      if f then f:close() filename = moonpath end
    end
    local f = io.open(filename, "rb")
    if f then
      local text = f:read("*a")
      f:close()
      if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end
      local fn, err
      if filename:match("moon$") then
        fn, err = moonloadstring(text, filename)
      else
        fn, err = loadstring(text, "@" .. filename)
      end
      if not fn then
        error(string.format('Error loading Lua module "%s":\n%s', filename, tostring(err)))
      end
      return fn
    end
  end
  return "\n\tno file for module " .. name
end
moonloadstring = require("moonscript").loadstring

-- The parts of the aegisub table Inspector.moon touches.
aegisub = {
  log = function(_, fmt, ...) io.stderr:write(string.format(fmt, ...)) end,
  decode_path = function(p) return (p:gsub("^%?script", fontdir)) end,
  -- No video is open, so the script's PlayRes sizes the canvas.
  video_size = function() return nil end,
  frame_from_ms = function() return nil end,
  ms_from_frame = function() return nil end,
}

local Inspector = require("SubInspector.Inspector")

-- requireffi must have loaded the packaged library, not some other copy.
local ffi = require("ffi")
local function realpath(p)
  local pipe = assert(io.popen("realpath -- '" .. p:gsub("'", "'\\''") .. "'"))
  local out = pipe:read("*l")
  pipe:close()
  return out
end
local loaded = {}
for line in io.lines("/proc/self/maps") do
  local path = line:match("%s(/%S*libSubInspector%.so)$")
  if path then loaded[realpath(path)] = true end
end
assert(loaded[realpath(library)], "the packaged libSubInspector.so is not the library requireffi loaded")
local n = 0
for _ in pairs(loaded) do n = n + 1 end
assert(n == 1, "more than one libSubInspector.so is loaded")

-- Inspector.moon 0.7.2 requires 0.5.1 or a later 0.5.x.
local version = ffi.load(library).si_getVersion()
local major, minor, patch =
  math.floor(version / 65536) % 256, math.floor(version / 256) % 256, version % 256
print(string.format("libSubInspector %d.%d.%d", major, minor, patch))
assert(major == 0 and minor == 5 and patch >= 1,
  string.format("libSubInspector %d.%d.%d is not 0.5.x (0.5.1 or later)", major, minor, patch))

local subs = {
  { class = "info", key = "PlayResX", value = "1280", raw = "PlayResX: 1280" },
  { class = "info", key = "PlayResY", value = "720", raw = "PlayResY: 720" },
  { class = "info", key = "ScaledBorderAndShadow", value = "yes", raw = "ScaledBorderAndShadow: yes" },
  { class = "style", name = "Default",
    raw = "Style: Default,DejaVu Sans,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1" },
  { class = "dialogue" },
}
-- The constructor rejects a library whose version Inspector.moon cannot use.
local inspector = Inspector(subs)

local function measure(text)
  local line = { start_time = 0, end_time = 1000, style = "Default", text = text,
    raw = "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,," .. text }
  local rects, times = inspector:getBounds({ line })
  assert(rects, times)
  local r = rects[1]
  if r == false then
    print(string.format("%-46s -> no bounds", text))
    return false
  end
  print(string.format("%-46s -> x=%d y=%d w=%d h=%d", text, r.x, r.y, r.w, r.h))
  return r
end

local a = measure("Hello world")
assert(a and a.w > 100 and a.h > 20, "a bottom-centred line has no plausible bounds")
assert(a.y + a.h <= 720 and a.y + a.h > 600, "a bottom-centred line is not near the bottom")
assert(math.abs((a.x + a.w / 2) - 640) < 20, "a bottom-centred line is not centred")
local b = measure("{\\an7\\pos(100,100)\\bord0}Hello world")
assert(b and math.abs(b.x - 100) <= 10 and b.y >= 100 and b.y - 100 <= 20,
  "an \\an7\\pos(100,100) line does not start at 100,100")
local c = measure("{\\an7\\pos(100,100)\\bord0\\fscx200}Hello world")
assert(c and c.w > 1.8 * b.w and c.w < 2.2 * b.w, "\\fscx200 did not double the width")
assert(measure("{\\alpha&HFF&}Hello world") == false, "an invisible line has bounds")
print("OK")
