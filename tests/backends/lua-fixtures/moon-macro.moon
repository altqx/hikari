-- L6 fixture: a MoonScript macro; the error is on line 7 (not a tail call).
export script_name = "Moon"

aegisub.register_macro "Moon", "", (subs) ->
  aegisub.log "%s", "moon ran " .. table.concat [tostring(x * 2) for x in *{1, 2, 3}], ","
aegisub.register_macro "Moon error", "", ->
  error "moon failure"
  nil
